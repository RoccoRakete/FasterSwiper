#include "src/public/fasterswiper.h"

#include "src/proto-util.h"
#include "src/public/fasterswiper.pb.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

using ::testing::Eq;

proto::DaemonOptions HydrateAndExtract(const proto::DaemonOptions& input) {
  std::string serialized = input.SerializeAsString();
  FS_DaemonOptions* options = nullptr;
  EXPECT_TRUE(FS_LoadDaemonOptionsFromBinaryProto(serialized.data(),
                                                  serialized.size(), &options));
  EXPECT_NE(options, nullptr);

  EXPECT_TRUE(FS_HydrateDaemonOptions(options));

  size_t out_len = 0;
  EXPECT_TRUE(FS_SaveDaemonOptionsToBinaryProto(options, nullptr, &out_len));
  std::string output_data(out_len, '\0');
  EXPECT_TRUE(
      FS_SaveDaemonOptionsToBinaryProto(options, output_data.data(), &out_len));
  EXPECT_TRUE(FS_DestroyDaemonOptions(options));

  proto::DaemonOptions result;
  EXPECT_TRUE(result.ParseFromString(output_data));
  return result;
}

TEST(FasterSwiperTest, DefaultDaemonOptions) {
  FS_DaemonOptions* options = nullptr;
  ASSERT_TRUE(FS_LoadDefaultDaemonOptions(&options));
  ASSERT_NE(options, nullptr);

  size_t out_len = 0;
  ASSERT_TRUE(FS_SaveDaemonOptionsToBinaryProto(options, nullptr, &out_len));
  std::string output_data(out_len, '\0');
  ASSERT_TRUE(
      FS_SaveDaemonOptionsToBinaryProto(options, output_data.data(), &out_len));
  ASSERT_TRUE(FS_DestroyDaemonOptions(options));

  proto::DaemonOptions result;
  ASSERT_TRUE(result.ParseFromString(output_data));

  EXPECT_TRUE(result.horizontal_settings().enabled());
  EXPECT_EQ(FromProtoDuration(result.horizontal_settings().duration()),
            absl::Milliseconds(200));
  EXPECT_EQ(result.horizontal_settings().easing_function(),
            proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);

  EXPECT_TRUE(result.vertical_settings().enabled());
  EXPECT_EQ(FromProtoDuration(result.vertical_settings().duration()),
            absl::Milliseconds(200));
  EXPECT_EQ(result.vertical_settings().easing_function(),
            proto::EASING_FUNCTION_QUINTIC_EASE_OUT);

  EXPECT_EQ(result.frames_per_second(), 240);
  EXPECT_TRUE(result.has_match_screen_refresh_rate());
  EXPECT_FALSE(result.match_screen_refresh_rate());
  EXPECT_TRUE(result.intercept_mission_control_shortcuts());
  EXPECT_TRUE(result.enable_jump_to_space_shortcuts());
  EXPECT_TRUE(result.has_jump_to_space_modifier());
  EXPECT_EQ(result.jump_to_space_modifier(),
            proto::JUMP_TO_SPACE_MODIFIER_COMMAND);
}

TEST(FasterSwiperTest, MissingRefreshModePreservesOlderManualTarget) {
  proto::DaemonOptions input;
  input.set_frames_per_second(73);
  input.set_enable_jump_to_space_shortcuts(false);
  const auto result = HydrateAndExtract(input);
  EXPECT_TRUE(result.has_match_screen_refresh_rate());
  EXPECT_FALSE(result.match_screen_refresh_rate());
  EXPECT_EQ(result.frames_per_second(), 73);
  EXPECT_FALSE(result.enable_jump_to_space_shortcuts());
}

TEST(FasterSwiperTest, RefreshToggleRoundTripsWithoutChangingManualOrOtherSettings) {
  for (int64_t manual : {1, 29, 73, 144, 360, 1000}) {
    proto::DaemonOptions options;
    options.set_frames_per_second(manual);
    options.set_jump_to_space_modifier(proto::JUMP_TO_SPACE_MODIFIER_CONTROL);
    options.set_intercept_mission_control_shortcuts(false);
    options.mutable_vertical_settings()->set_enabled(false);
    for (bool automatic : {true, false, true}) {
      options.set_match_screen_refresh_rate(automatic);
      options = HydrateAndExtract(options);
      EXPECT_EQ(options.match_screen_refresh_rate(), automatic);
      EXPECT_EQ(options.frames_per_second(), manual);
      EXPECT_EQ(options.jump_to_space_modifier(), proto::JUMP_TO_SPACE_MODIFIER_CONTROL);
      EXPECT_FALSE(options.intercept_mission_control_shortcuts());
      EXPECT_FALSE(options.vertical_settings().enabled());
      EXPECT_EQ(HydrateAndExtract(options).SerializeAsString(), options.SerializeAsString());
    }
  }
}

TEST(FasterSwiperTest, MissingModifierHydratesToCommand) {
  proto::DaemonOptions input;
  input.set_frames_per_second(120);
  input.set_enable_jump_to_space_shortcuts(false);
  auto output = HydrateAndExtract(input);
  EXPECT_TRUE(output.has_jump_to_space_modifier());
  EXPECT_EQ(output.jump_to_space_modifier(),
            proto::JUMP_TO_SPACE_MODIFIER_COMMAND);
  EXPECT_EQ(output.frames_per_second(), 120);
  EXPECT_FALSE(output.enable_jump_to_space_shortcuts());
}

TEST(FasterSwiperTest, ModifierSelectionSurvivesHydrationAndSerialization) {
  for (auto modifier : {proto::JUMP_TO_SPACE_MODIFIER_COMMAND,
                        proto::JUMP_TO_SPACE_MODIFIER_CONTROL}) {
    proto::DaemonOptions input;
    input.set_jump_to_space_modifier(modifier);
    input.mutable_horizontal_settings()->set_enabled(false);
    input.mutable_vertical_settings()->set_enabled(false);
    input.set_intercept_mission_control_shortcuts(false);
    auto output = HydrateAndExtract(input);
    EXPECT_TRUE(output.has_jump_to_space_modifier());
    EXPECT_EQ(output.jump_to_space_modifier(), modifier);
    EXPECT_FALSE(output.horizontal_settings().enabled());
    EXPECT_FALSE(output.vertical_settings().enabled());
    EXPECT_FALSE(output.intercept_mission_control_shortcuts());
    EXPECT_EQ(HydrateAndExtract(output).SerializeAsString(),
              output.SerializeAsString());
  }
}

TEST(FasterSwiperTest, MigrateLegacyDefaultsNotCopied) {
  proto::DaemonOptions input;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  *input.mutable_animation_duration_per_space() =
      ToProtoDuration(absl::Milliseconds(200));
  input.set_easing_function(proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);
#pragma clang diagnostic pop

  proto::DaemonOptions output = HydrateAndExtract(input);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  EXPECT_FALSE(output.has_animation_duration_per_space());
  EXPECT_FALSE(output.has_easing_function());
  EXPECT_FALSE(output.has_cubic_bezier_curve());
#pragma clang diagnostic pop

  EXPECT_TRUE(output.horizontal_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.horizontal_settings().duration()),
            absl::Milliseconds(200));
  EXPECT_EQ(output.horizontal_settings().easing_function(),
            proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);

  EXPECT_TRUE(output.vertical_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.vertical_settings().duration()),
            absl::Milliseconds(200));
  EXPECT_EQ(output.vertical_settings().easing_function(),
            proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
}

TEST(FasterSwiperTest, MigrateLegacyCustomValuesCopied) {
  proto::DaemonOptions input;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  *input.mutable_animation_duration_per_space() =
      ToProtoDuration(absl::Milliseconds(450));
  input.set_easing_function(proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
  input.mutable_cubic_bezier_curve()->set_p1x(0.1);
  input.mutable_cubic_bezier_curve()->set_p1y(0.2);
  input.mutable_cubic_bezier_curve()->set_p2x(0.3);
  input.mutable_cubic_bezier_curve()->set_p2y(0.4);
#pragma clang diagnostic pop

  proto::DaemonOptions output = HydrateAndExtract(input);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  EXPECT_FALSE(output.has_animation_duration_per_space());
  EXPECT_FALSE(output.has_easing_function());
  EXPECT_FALSE(output.has_cubic_bezier_curve());
#pragma clang diagnostic pop

  EXPECT_TRUE(output.horizontal_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.horizontal_settings().duration()),
            absl::Milliseconds(450));
  EXPECT_EQ(output.horizontal_settings().easing_function(),
            proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
  EXPECT_DOUBLE_EQ(output.horizontal_settings().cubic_bezier_curve().p1x(),
                   0.1);
  EXPECT_DOUBLE_EQ(output.horizontal_settings().cubic_bezier_curve().p1y(),
                   0.2);
  EXPECT_DOUBLE_EQ(output.horizontal_settings().cubic_bezier_curve().p2x(),
                   0.3);
  EXPECT_DOUBLE_EQ(output.horizontal_settings().cubic_bezier_curve().p2y(),
                   0.4);

  EXPECT_TRUE(output.vertical_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.vertical_settings().duration()),
            absl::Milliseconds(450));
  EXPECT_EQ(output.vertical_settings().easing_function(),
            proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
  EXPECT_DOUBLE_EQ(output.vertical_settings().cubic_bezier_curve().p1x(), 0.1);
  EXPECT_DOUBLE_EQ(output.vertical_settings().cubic_bezier_curve().p1y(), 0.2);
  EXPECT_DOUBLE_EQ(output.vertical_settings().cubic_bezier_curve().p2x(), 0.3);
  EXPECT_DOUBLE_EQ(output.vertical_settings().cubic_bezier_curve().p2y(), 0.4);
}

TEST(FasterSwiperTest, PreserveExistingSettings) {
  proto::DaemonOptions input;
  input.mutable_horizontal_settings()->set_enabled(false);
  *input.mutable_horizontal_settings()->mutable_duration() =
      ToProtoDuration(absl::Milliseconds(120));

  proto::DaemonOptions output = HydrateAndExtract(input);

  EXPECT_FALSE(output.horizontal_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.horizontal_settings().duration()),
            absl::Milliseconds(120));
  EXPECT_EQ(output.horizontal_settings().easing_function(),
            proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);

  EXPECT_TRUE(output.vertical_settings().enabled());
  EXPECT_EQ(FromProtoDuration(output.vertical_settings().duration()),
            absl::Milliseconds(200));
  EXPECT_EQ(output.vertical_settings().easing_function(),
            proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
}

}  // namespace
}  // namespace fasterswiper
