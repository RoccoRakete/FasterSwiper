#include "src/event.h"
#include "src/macos-private.h"

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <gtest/gtest.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

class GeneratedEventTest : public testing::Test {
 protected:
  std::optional<int> original_os_ = absl::GetFlag(FLAGS_force_os_version);
  void TearDown() override {
    absl::SetFlag(&FLAGS_force_os_version, original_os_);
  }
  void CheckAxis(int direction) {
    for (int os : {26, 27, 28}) {
      absl::SetFlag(&FLAGS_force_os_version, os);
      for (bool natural : {false, true}) {
        for (int phase : {kGestureBegan, kGestureChanged, kGestureEnded,
                          kGestureCancelled}) {
          for (double progress : {-0.329223, 0.0, 0.329223}) {
            for (std::optional<double> velocity :
                 {std::optional<double>{}, std::optional<double>{-0.125},
                  std::optional<double>{0.125}}) {
              SCOPED_TRACE(testing::Message() << os << " " << natural << " "
                           << direction << " " << phase << " " << progress);
              auto event = CreateDockControlGestureEvent(
                  phase, direction, progress, velocity, natural);
              ASSERT_NE(event.get(), nullptr);
              // Decode actual CGEvent fields, not a duplicated normalization helper.
              const double sign = direction == kCGGestureMotionHorizontal &&
                                          os >= 27 && natural ? -1 : 1;
              EXPECT_EQ(CGEventGetIntegerValueField(event.get(),
                            kCGEventGesturePhase), phase);
              EXPECT_EQ(CGEventGetIntegerValueField(event.get(),
                            kCGEventGestureSwipeMotion), direction);
              EXPECT_FLOAT_EQ(CGEventGetDoubleValueField(event.get(),
                                   kCGEventGestureSwipeProgress), sign * progress);
              EXPECT_FLOAT_EQ(CGEventGetDoubleValueField(event.get(),
                                   kCGEventGestureSwipeVelocityX),
                               direction == kCGGestureMotionHorizontal
                                   ? sign * velocity.value_or(0) : 0);
              EXPECT_FLOAT_EQ(CGEventGetDoubleValueField(event.get(),
                                   kCGEventGestureSwipeVelocityY),
                               direction == kCGGestureMotionVertical
                                   ? sign * velocity.value_or(0) : 0);
            }
          }
        }
      }
    }
  }
};

TEST_F(GeneratedEventTest, VerticalRetainsCanonicalProgressAndVelocity) {
  CheckAxis(kCGGestureMotionVertical);
}
TEST_F(GeneratedEventTest, HorizontalRetainsExistingNaturalScrollingAdaptation) {
  CheckAxis(kCGGestureMotionHorizontal);
}

}  // namespace
}  // namespace fasterswiper
