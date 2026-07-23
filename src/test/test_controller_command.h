#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>
#include <cstddef>
#include <cstring>

enum class SigurdOSTestCommandParseResult : uint8_t {
  Ok,
  Empty,
  Unknown,
  TooLong,
};

struct SigurdOSTestCommandLine {
  char command[24];
  char arguments[232];
};

inline bool sigurdos_test_controller_command_known_for_profile(const char* command,
                                                               bool radio_enabled) {
  if (!command) return false;
  if (std::strcmp(command, "simulateack") == 0) return !radio_enabled;
  static const char* const commands[] = {
      "help", "?", "nav", "navigate", "back", "tb", "trackball", "type",
      "picker", "press", "inject", "msg", "sendchannel", "sendmessage", "senddm",
      "opendm", "addchannel", "addchan", "removechannel", "rmchannel", "removechan",
      "rmchan", "addrepeater", "addroomserver", "login", "setlogin", "setloginguest",
      "screen", "status", "stresschat", "keydiag", "kbddiag", "inputdiag",
      "touchdiag", "trackballdiag", "tbdiag", "gpsdiag", "contactstats", "ble",
      "debug", "emoji", "emoji-ac", "capture", "acmd", "loginstat", "tree",
      "widgets", "telemetry", "query", "crash", "drift", "scrolllist", "tap",
      "backlight", "fetchmsgs", "getrf", "setrf", "reboot", "restart",
      "factoryreset", "wipe", "advert"};
  for (const char* known : commands) {
    if (std::strcmp(command, known) == 0) return true;
  }
  return false;
}

inline bool sigurdos_test_controller_command_known(const char* command) {
#if defined(SIGURDOS_REMOTE_TEST_RADIO) && SIGURDOS_REMOTE_TEST_RADIO
  constexpr bool radio_enabled = true;
#else
  constexpr bool radio_enabled = false;
#endif
  return sigurdos_test_controller_command_known_for_profile(command, radio_enabled);
}

inline SigurdOSTestCommandParseResult sigurdos_test_controller_parse_command(
    const char* line, SigurdOSTestCommandLine* out) {
  if (!line || !out) return SigurdOSTestCommandParseResult::Empty;
  while (*line && std::isspace(static_cast<unsigned char>(*line))) ++line;
  if (!*line || *line == '#' || *line == ';') return SigurdOSTestCommandParseResult::Empty;
  const size_t total = std::strlen(line);
  if (total >= sizeof(out->command) + sizeof(out->arguments)) {
    return SigurdOSTestCommandParseResult::TooLong;
  }

  const char* split = line;
  while (*split && !std::isspace(static_cast<unsigned char>(*split))) ++split;
  const size_t command_len = static_cast<size_t>(split - line);
  if (command_len == 0 || command_len >= sizeof(out->command)) {
    return SigurdOSTestCommandParseResult::TooLong;
  }
  std::memcpy(out->command, line, command_len);
  out->command[command_len] = '\0';

  while (*split && std::isspace(static_cast<unsigned char>(*split))) ++split;
  size_t args_len = std::strlen(split);
  while (args_len > 0 &&
         std::isspace(static_cast<unsigned char>(split[args_len - 1]))) {
    --args_len;
  }
  if (args_len >= sizeof(out->arguments)) return SigurdOSTestCommandParseResult::TooLong;
  std::memcpy(out->arguments, split, args_len);
  out->arguments[args_len] = '\0';
  return sigurdos_test_controller_command_known(out->command)
             ? SigurdOSTestCommandParseResult::Ok
             : SigurdOSTestCommandParseResult::Unknown;
}
