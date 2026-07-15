#!/usr/bin/env python3
"""
Split sigurd_mesh_v2.h into .h (declarations + inline) and .cpp (implementations).
Processes from bottom to top to preserve positions.
"""
import re, os

HEADER = "src/mesh/sigurd_mesh_v2.h"
CPP   = "src/mesh/sigurd_mesh_v2.cpp"

# Back up original
import shutil
shutil.copy2(HEADER, HEADER + ".bak")

with open(HEADER) as f:
    content = f.read()

KEEP_INLINE = {
    "SigurdMeshV2", "~SigurdMeshV2",
    "getOwnName", "setOwnName",
    "getSignalHistoryCount", "getSignalHistoryRSSI", "getSignalHistorySNR",
    "hasTraceResult", "getTracePathLen", "clearTraceResult",
    "pingIsActive", "pingOnCooldown", "pingCooldownRemaining", "activePingRemaining",
    "getPingResultCount", "getPingResult",
    "getResponseCount", "getResponse", "clearResponses",
    "getRoomMsgFetchCount", "getRoomMsgFetchEntry", "clearRoomMsgFetch",
    "isDiscoveryComplete", "getPathLen",
    "onSendTimeout", "isAutoAddEnabled", "shouldOverwriteWhenFull",
    "onContactsFull", "getExtraAckTransmitCount", "getAutoAddMaxHops",
    "calcFloodTimeoutMillisFor", "calcDirectTimeoutMillisFor",
    "findLoginEntry", "removeLoginEntry", "isLoggedIn",
    "getLoginPermission", "getLoginStatus",
    "getContactCount", "getChannelName",
    "clearActiveScope", "setSendUnscopedOnce", "isActiveScopeNull", "copyActiveScope",
    "setDutyCycle", "getAirtimeBudgetFactor",
    "sendScopedImpl", "sendFloodScoped",
}

def find_method(text, name, start_pos=0):
    """Find a method definition and extract its full text.
    Returns (body_start_pos, body_end_pos, full_method_text) or None."""
    idx = text.find(name, start_pos)
    if idx < 0:
        return None
    
    # Verify this is a method definition
    prefix = text[idx-1:idx] if idx > 0 else ' '
    if not (prefix.isspace() or prefix in '*&>~'):
        return find_method(text, name, idx + 1)
    
    # Find the start of the declaration (include return type)
    decl_start = idx
    # Walk backwards to find the beginning of the declaration
    # Stop at start-of-line after whitespace, or after '>' for templates
    in_template = False
    paren_depth_back = 0
    temp_idx = idx - 1
    while temp_idx >= 0:
        c = text[temp_idx]
        if c in ' \t\r\n':
            # Check if previous non-whitespace is a line start
            temp_idx -= 1
            continue
        elif c == '>':
            in_template = True
            temp_idx -= 1
            continue
        elif c == '<' and in_template:
            in_template = False
            temp_idx -= 1
            continue
        else:
            # Not whitespace and not template. This is the last char of the return type.
            # Find the beginning of the return type (start of line after whitespace)
            # Actually, just find the last newline before the declaration
            nl = text.rfind('\n', 0, temp_idx)
            if nl >= 0:
                decl_start = nl + 1
                # Skip leading whitespace on this line
                while decl_start < idx and text[decl_start] in ' \t':
                    decl_start += 1
            break
    
    after_name = text[idx + len(name):]
    if not after_name.startswith('('):
        return find_method(text, name, idx + 1)
    
    # Skip parameter list
    paren_depth = 1
    j = idx + len(name) + 1
    in_str = False
    in_char = False
    while j < len(text) and paren_depth > 0:
        c = text[j]
        if c == '"' and not in_char: in_str = not in_str
        elif c == "'" and not in_str: in_char = not in_char
        elif not in_str and not in_char:
            if c == '(': paren_depth += 1
            elif c == ')':
                paren_depth -= 1
                if paren_depth == 0:
                    break
        j += 1
    
    if paren_depth != 0:
        return find_method(text, name, idx + 1)
    
    # Skip possible keywords after ')'
    k = j + 1
    while k < len(text):
        c = text[k]
        if c in ' \t\n\r':
            k += 1
        elif c.isalpha():
            kw_end = k
            while kw_end < len(text) and text[kw_end].isalpha():
                kw_end += 1
            kw = text[k:kw_end]
            if kw in ('const', 'override', 'noexcept', 'final'):
                k = kw_end
            else:
                break
        else:
            break
    
    # Skip to '{'
    while k < len(text) and text[k] in ' \t\n\r':
        k += 1
    
    if text[k:k+1] != '{':
        return find_method(text, name, idx + 1)
    
    # Find matching close brace
    depth = 0
    in_str = False
    in_char = False
    in_block = False
    l = k
    while l < len(text):
        c = text[l]
        if c == '"' and not in_char: in_str = not in_str
        elif c == "'" and not in_str: in_char = not in_char
        elif not in_str and not in_char:
            if c == '/' and l+1 < len(text) and text[l+1] == '*':
                in_block = True
                l += 2
                continue
            if in_block:
                if c == '*' and l+1 < len(text) and text[l+1] == '/':
                    in_block = False
                    l += 2
                    continue
                l += 1
                continue
            if c == '{': depth += 1
            elif c == '}': depth -= 1
        if depth == 0:
            return (k, l + 1, text[decl_start:l+1])
        l += 1
    return None

# ── Step 1: Find all methods ──
all_methods = []
for m in re.finditer(r'^\s+(?:const\s+)?(?:\w+(?:\s*<[^>]+>)?(?:\s*\*)?(?:\s*&)?(?:\s+const\s*)?(?:\s+\*)?\s+)(\w+)\s*\(', content, re.MULTILINE):
    name = m.group(1)
    if name in ('if', 'for', 'while', 'switch', 'else', 'new', 'delete', 'sizeof',
                'return', 'const', 'constexpr', 'static', 'virtual', 'mutable',
                'case', 'int', 'bool', 'void', 'uint8_t', 'uint16_t', 'uint32_t',
                'size_t', 'float', 'double', 'char', 'int8_t', 'int16_t', 'int32_t',
                'unsigned', 'signed', 'long', 'short', 'auto', 'extern', 'register',
                'volatile', 'inline', 'explicit', 'friend', 'typedef', 'using',
                'namespace', 'class', 'struct', 'enum', 'template', 'typename',
                'public', 'private', 'protected', 'virtual', 'override', 'final',
                'true', 'false', 'nullptr', 'NULL', 'this'):
        continue
    mb = find_method(content, name)
    if mb:
        body_start, body_end, full_text = mb
        all_methods.append({
            'name': name,
            'body_start': body_start,
            'body_end': body_end,
            'full_text': full_text,
            'keep': name in KEEP_INLINE
        })

print(f"Found {len(all_methods)} methods")

# ── Step 2: Build .cpp content ──
# Includes needed
ns_open = "namespace sigurdos {\nnamespace mesh {"
ns_close = "} // namespace mesh\n} // namespace sigurdos"

cpp_lines = []
cpp_lines.append("// SPDX-License-Identifier: GPL-3.0-or-later")
cpp_lines.append("// Copyright (C) 2025 Ben")
cpp_lines.append("//")
cpp_lines.append("// SigurdMeshV2 — method implementations split from sigurd_mesh_v2.h")
cpp_lines.append("//")
cpp_lines.append('#include "sigurd_mesh_v2.h"')
cpp_lines.append('#include <cstring>')
cpp_lines.append('#include <cstdlib>')
cpp_lines.append('#include <cstdio>')
cpp_lines.append('#include "mesh_wrapper.h"')
cpp_lines.append('#include "hal/prefs.h"')
cpp_lines.append('#include "hal/battery.h"')
cpp_lines.append('#include "hal/gps.h"')
cpp_lines.append('#include "hal/tdeck_board.h"')
cpp_lines.append('#include "../diagnostics/debug_cfg.h"')
cpp_lines.append('')
cpp_lines.append(ns_open)
cpp_lines.append('')

# Sort methods by position (top-to-bottom)
sorted_methods = sorted(all_methods, key=lambda m: m['body_start'])

# Keep track of which methods we've already added to avoid duplicates
added_names = set()

for m in sorted_methods:
    if m['keep']:
        continue  # Skip inline methods
    
    name = m['name']
    if name in added_names:
        continue
    added_names.add(name)
    
    # Get the full method text and add the SigurdMeshV2:: qualifier
    full = m['full_text']
    
    # Find the return type by looking at the part before the method name
    body_start = m['body_start']
    # Get just the signature
    sig_end = full.find('{')
    if sig_end < 0:
        continue
    sig = full[:sig_end]
    
    # Insert scope qualifier: method_name -> SigurdMeshV2::method_name
    # Find the method name in the signature
    name_pos = sig.rfind(name)
    if name_pos < 0:
        continue
    
    # The qualified name
    qualified_sig = sig[:name_pos] + "SigurdMeshV2::" + sig[name_pos:]
    
    # Get the body (including opening {)
    body = full[sig_end:]
    
    # Write qualified method to .cpp
    cpp_lines.append('')  # blank line before method
    cpp_lines.append(qualified_sig + body)
    
    print(f"  Added {name} to .cpp")

cpp_lines.append('')
cpp_lines.append(ns_close)
cpp_lines.append('')

cpp_content = '\n'.join(cpp_lines)

# ── Step 3: Build new header ──
# Write header
# Process from bottom to top: replace each moved method body with ';'
new_header = content

# Sort methods by position (descending) to process bottom-up
moved = sorted([m for m in all_methods if not m['keep']], 
               key=lambda m: -m['body_end'])

for m in moved:
    name = m['name']
    body_end = m['body_end']
    full = m['full_text']
    sig_end = full.find('{')
    if sig_end < 0:
        continue
    
    signature = full[:sig_end].rstrip()
    method_start = m['body_end'] - len(full)
    
    # Replace the signature + body with just the signature + ";"
    decl_with_semi = signature + ";\n"
    new_header = new_header[:method_start] + decl_with_semi + new_header[m['body_end']:]

# The tail of the original file (after the last method body, including private:
# section, member variables, class close, and namespace closes) should be preserved.
# Ensure the class closing }; and namespace closes are present
if not new_header.strip().endswith("} // namespace sigurdos"):
    # Append the missing closing structure from the backup
    with open(HEADER + ".bak") as f:
        bak_content = f.read()
    # Find ";  // class close" and namespace closes in the backup
    class_close = bak_content.rfind("};\n\n} // namespace mesh\n} // namespace sigurdos")
    if class_close >= 0:
        tail_to_append = bak_content[class_close:]
        # Only append if not already present
        if tail_to_append.strip() not in new_header:
            new_header = new_header.rstrip() + "\n" + tail_to_append + "\n"

with open(HEADER, 'w') as f:
    f.write(new_header)

# Write .cpp
with open(CPP, 'w') as f:
    f.write(cpp_content)

print(f"\nDone!")
print(f"  Header: {HEADER}")
print(f"  CPP:    {CPP}")
print(f"  Methods moved: {len(added_names)}")
print(f"Backup: {HEADER}.bak")
