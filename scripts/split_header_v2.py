#!/usr/bin/env python3
"""
Simplified split: find class-level (depth = 1) brace pairs to identify methods,
extract non-KEEP_INLINE method bodies to .cpp, leave declarations in header.
"""
import re, os

HEADER = "src/mesh/sigurd_mesh_v2.h"
CPP = "src/mesh/sigurd_mesh_v2.cpp"

# Restore backup
import shutil
shutil.copy2(HEADER + ".bak", HEADER)

with open(HEADER) as f:
    content = f.read()

KEEP = {
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

# Strategy: find every method body at the class level (depth == 1).
# Extract method name from the declaration before '{'.
# If name NOT in KEEP, replace body with ';' in header and append to .cpp.

# Track depth character by character, recording character positions
# where depth == 1 (class level) and we encounter '{' or '}'.

# Find the class opening
class_open = content.find('{', content.find('class SigurdMeshV2'))
# Find the class closing — scan the whole file tracking depth from the beginning.
# The class opens at the `class SigurdMeshV2 {` line. It closes at the LAST `};` at depth=1.
depth = 0
class_depth_start = None
class_close = None

in_str = False
in_char = False
in_block = False
for i in range(len(content)):
    c = content[i]
    
    if c == '"' and not in_char: in_str = not in_str
    elif c == "'" and not in_str: in_char = not in_char
    elif not in_str and not in_char:
        if c == '/' and i+1 < len(content) and content[i+1] == '*':
            in_block = True
        if in_block:
            if c == '*' and i+1 < len(content) and content[i+1] == '/':
                in_block = False
            continue
        
        if c == '{':
            if 'class SigurdMeshV2' in content[max(0,i-200):i] and class_depth_start is None:
                class_depth_start = depth  # remember the depth right before class opening
            depth += 1
        elif c == '}':
            depth -= 1
            # Check for class closing: at depth == class_depth_start
            if class_depth_start is not None and depth == class_depth_start:
                if i+1 < len(content) and content[i+1] == ';':
                    class_close = i  # keep updating — last `};` at class level wins
                    # Don't break; we want the LAST one

if class_close is None:
    print("ERROR: could not find class close")
    sys.exit(1)

print(f"Class open depth: {class_depth_start}, class close at char {class_close}")

print(f"Class body: chars {class_open}..{class_close}")

# Now find all class-level (depth=1) brace pairs.
# When we're at depth=1 and encounter '{', depth goes to 2.
# When at depth=2 and encounter '}', depth goes back to 1.
# Each such transition is a class-level method/struct body.

depth = 0
brace_starts = []  # positions of '{' when depth goes 1->2
brace_ends = []    # positions of '}' when depth goes 2->1
in_class = False

for i in range(class_open, class_close):
    c = content[i]
    if c == '{':
        if depth == 1:
            brace_starts.append(i)
        depth += 1
    elif c == '}':
        depth -= 1
        if depth == 1:
            brace_ends.append(i)

print(f"Found {len(brace_starts)} class-level brace pairs")
print(f"Found {len(brace_ends)} closing braces")

# Sanity: should have equal numbers
n = min(len(brace_starts), len(brace_ends))
print(f"Processing {n} pairs")

# For each class-level block, determine if it's a method or struct
# and extract the method name
blocks = []
for j in range(n):
    start = brace_starts[j]
    end = brace_ends[j]
    
    # Get the signature line (before the opening brace)
    # Walk backwards from start to find the beginning of the declaration
    sig_start = start
    while sig_start > class_open + 1:
        if content[sig_start-1] == '\n':
            break
        sig_start -= 1
    
    sig_line = content[sig_start:start].strip()
    
    # Check if it's a method (has '(' before the brace)
    # or a struct/class/enum/if/for/while
    if '(' in sig_line:
        # Probably a method — extract the name
        # Pattern: return_type method_name(params) [const] [override]
        # Find method_name before '('
        paren_pos = sig_line.find('(')
        before_paren = sig_line[:paren_pos].strip()
        
        # Get the last word before '(' — this is the function name
        # But skip if the word is a keyword that indicates control flow
        # Actually, we need to handle: "void setName(...)" -> name=setName
        # and "for (int i..." -> not a method
        
        # Simplified: check if sig_line contains any of these at the start
        words = sig_line.split()
        first_word = words[0] if words else ""
        
        if first_word in ('if', 'for', 'while', 'switch', 'else', 'struct', 'enum',
                          'class', 'catch', 'template'):
            blocks.append(('ctrl', -1, sig_start, end))
            continue
        
        # Extract method name: last occurrence of identifier before '('
        # that's not 'const', 'override', etc.
        m = re.match(r'.*?(\w[\w:~]*)\s*\(', sig_line)
        if m:
            name = m.group(1)
            # Clean up ~ for destructors
            name_clean = name.lstrip('~')
            
            if name_clean in KEEP:
                blocks.append(('keep', name_clean, sig_start, end))
            else:
                blocks.append(('move', name_clean, sig_start, end))
        else:
            blocks.append(('unknown', -1, sig_start, end))
    else:
        # No parenthesis — struct/array/initializer
        if '{' in sig_line or sig_line.startswith('struct') or sig_line.startswith('enum'):
            blocks.append(('struct', -1, sig_start, end))
        else:
            blocks.append(('other', -1, sig_start, end))

print(f"\nBlock classification:")
keep_count = 0
move_count = 0
struct_count = 0
other_count = 0
for blk in blocks:
    if blk[0] == 'keep':
        keep_count += 1
        print(f"  KEEP: {blk[1]} (chars {blk[3]-blk[2]} range)")
    elif blk[0] == 'move':
        move_count += 1
        print(f"  MOVE: {blk[1]} (chars {blk[3]-blk[2]} range)")
    elif blk[0] == 'struct':
        struct_count += 1

print(f"\nKEEP: {keep_count}, MOVE: {move_count}, STRUCT/CTRL: {struct_count}")
