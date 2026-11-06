#pragma once

#include "src/event.h"
#include "src/hotkeys.h"
#include "src/public/fasterswiper.pb.h"

#include <cstdint>
#include <optional>

namespace fasterswiper {

// Pure recognition only: never posts events, reads preferences, or switches
// spaces. Keep this shared by the event handler and regression tests.
inline std::optional<int64_t> JumpToSpaceIndex(
    const KeyEvent& event, const proto::DaemonOptions& options) {
  if (event.key_state != KeyState::kDown ||
      !options.enable_jump_to_space_shortcuts() ||
      (options.has_horizontal_settings() &&
       !options.horizontal_settings().enabled())) {
    return std::nullopt;
  }

  CGEventFlags required_modifier;
  switch (options.jump_to_space_modifier()) {
    case proto::JUMP_TO_SPACE_MODIFIER_COMMAND:
      required_modifier = kCGEventFlagMaskCommand;
      break;
    case proto::JUMP_TO_SPACE_MODIFIER_CONTROL:
      required_modifier = kCGEventFlagMaskControl;
      break;
    default:
      return std::nullopt;
  }
  // Preserve upstream's exact modifier matching, including Caps Lock handling.
  // Non-modifier bits (e.g. device-specific flags) do not affect matching.
  if ((event.modifiers & kModifierKeyMask) != required_modifier) {
    return std::nullopt;
  }

  // Physical top-row number key codes, preserving upstream's layout behavior.
  switch (event.key_code) {
    case 18:
      return 0;
    case 19:
      return 1;
    case 20:
      return 2;
    case 21:
      return 3;
    case 23:
      return 4;
    case 22:
      return 5;
    case 26:
      return 6;
    case 28:
      return 7;
    case 25:
      return 8;
    case 29:
      return 9;
    default:
      return std::nullopt;
  }
}

}  // namespace fasterswiper
