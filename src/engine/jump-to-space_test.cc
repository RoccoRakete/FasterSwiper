#include "src/engine/jump-to-space.h"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

proto::DaemonOptions EnabledOptions(proto::JumpToSpaceModifier modifier) {
  proto::DaemonOptions options;
  options.set_enable_jump_to_space_shortcuts(true);
  options.mutable_horizontal_settings()->set_enabled(true);
  options.set_jump_to_space_modifier(modifier);
  return options;
}

TEST(JumpToSpaceTest, BothModifiersMapAllTenNumberKeys) {
  constexpr std::array<CGKeyCode, 10> keys = {18, 19, 20, 21, 23,
                                              22, 26, 28, 25, 29};
  for (auto modifier : {proto::JUMP_TO_SPACE_MODIFIER_COMMAND,
                        proto::JUMP_TO_SPACE_MODIFIER_CONTROL}) {
    auto options = EnabledOptions(modifier);
    const auto flags = modifier == proto::JUMP_TO_SPACE_MODIFIER_COMMAND
                           ? kCGEventFlagMaskCommand
                           : kCGEventFlagMaskControl;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      EXPECT_EQ(JumpToSpaceIndex({keys[i], flags, KeyState::kDown}, options),
                static_cast<int64_t>(i));
    }
  }
}

TEST(JumpToSpaceTest, MissingModifierDefaultsToCommand) {
  auto options = EnabledOptions(proto::JUMP_TO_SPACE_MODIFIER_CONTROL);
  options.clear_jump_to_space_modifier();
  EXPECT_EQ(
      JumpToSpaceIndex({18, kCGEventFlagMaskCommand, KeyState::kDown}, options),
      0);
  EXPECT_FALSE(JumpToSpaceIndex({18, kCGEventFlagMaskControl, KeyState::kDown},
                                options));
}

TEST(JumpToSpaceTest, InactiveAndAdditionalModifiersPassThrough) {
  for (auto modifier : {proto::JUMP_TO_SPACE_MODIFIER_COMMAND,
                        proto::JUMP_TO_SPACE_MODIFIER_CONTROL}) {
    auto options = EnabledOptions(modifier);
    const auto required = modifier == proto::JUMP_TO_SPACE_MODIFIER_COMMAND
                              ? kCGEventFlagMaskCommand
                              : kCGEventFlagMaskControl;
    const auto other = modifier == proto::JUMP_TO_SPACE_MODIFIER_COMMAND
                           ? kCGEventFlagMaskControl
                           : kCGEventFlagMaskCommand;
    EXPECT_FALSE(JumpToSpaceIndex({18, 0, KeyState::kDown}, options));
    EXPECT_FALSE(JumpToSpaceIndex({18, other, KeyState::kDown}, options));
    for (auto extra : {kCGEventFlagMaskShift, kCGEventFlagMaskAlternate,
                       kCGEventFlagMaskAlphaShift, other}) {
      EXPECT_FALSE(
          JumpToSpaceIndex({18, required | extra, KeyState::kDown}, options));
    }
    // Device-specific and non-modifier bits were ignored by upstream too.
    EXPECT_EQ(JumpToSpaceIndex({18, required | 0x8, KeyState::kDown}, options),
              0);
  }
}

TEST(JumpToSpaceTest, KeyUpArrowsLettersAndNumpadPassThrough) {
  auto options = EnabledOptions(proto::JUMP_TO_SPACE_MODIFIER_COMMAND);
  EXPECT_FALSE(
      JumpToSpaceIndex({18, kCGEventFlagMaskCommand, KeyState::kUp}, options));
  for (CGKeyCode key : {0, 36, 82, 83, 123, 124, 125, 126}) {
    EXPECT_FALSE(JumpToSpaceIndex(
        {key, kCGEventFlagMaskCommand, KeyState::kDown}, options));
  }
}

TEST(JumpToSpaceTest, MissionControlArrowMatchingIsUnchanged) {
  auto options = EnabledOptions(proto::JUMP_TO_SPACE_MODIFIER_COMMAND);
  for (CGKeyCode key : {123, 124, 125, 126}) {
    const Hotkey hotkey{true, key, kCGEventFlagMaskControl};
    const KeyEvent event{key, kCGEventFlagMaskControl, KeyState::kDown};
    EXPECT_TRUE(event.ConcernsHotkey(hotkey));
    EXPECT_FALSE(JumpToSpaceIndex(event, options));
    EXPECT_FALSE((KeyEvent{key, kCGEventFlagMaskCommand, KeyState::kDown})
                     .ConcernsHotkey(hotkey));
  }
}

TEST(JumpToSpaceTest, DisabledShortcutsOrHorizontalAnimationPassThrough) {
  for (auto modifier : {proto::JUMP_TO_SPACE_MODIFIER_COMMAND,
                        proto::JUMP_TO_SPACE_MODIFIER_CONTROL}) {
    auto options = EnabledOptions(modifier);
    const auto flags = modifier == proto::JUMP_TO_SPACE_MODIFIER_COMMAND
                           ? kCGEventFlagMaskCommand
                           : kCGEventFlagMaskControl;
    options.set_enable_jump_to_space_shortcuts(false);
    EXPECT_FALSE(JumpToSpaceIndex({18, flags, KeyState::kDown}, options));
    options.set_enable_jump_to_space_shortcuts(true);
    options.mutable_horizontal_settings()->set_enabled(false);
    EXPECT_FALSE(JumpToSpaceIndex({18, flags, KeyState::kDown}, options));
    options.mutable_horizontal_settings()->set_enabled(true);
    options.mutable_vertical_settings()->set_enabled(false);
    options.set_intercept_mission_control_shortcuts(false);
    EXPECT_EQ(JumpToSpaceIndex({18, flags, KeyState::kDown}, options), 0);
    options.clear_horizontal_settings();
    EXPECT_EQ(JumpToSpaceIndex({18, flags, KeyState::kDown}, options), 0);
  }
}

}  // namespace
}  // namespace fasterswiper
