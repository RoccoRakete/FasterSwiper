#include "src/engine/physical-event-handler.h"

#include "src/cf-util.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"

#include <chrono>
#include <thread>

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <gtest/gtest.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

// Use real axis conversions and limits, but never query or send to WindowServer.
SpaceState TestSpaceState() {
  return SpaceState(WrapCFUnique(CFStringCreateWithCString(
                        nullptr, "test-display", kCFStringEncodingUTF8)),
                    {Space{.id = 1, .is_desktop = true},
                     Space{.id = 2, .is_desktop = true},
                     Space{.id = 3, .is_desktop = true}}, 0);
}

class NoPostSink : public CGEventSink {
public:
  void Post(CGEventRef event) override { ADD_FAILURE() << "Unexpected post"; }
};

// The real handler and animator run unchanged. This operation deliberately
// cannot post events, including from timer callbacks and animator destruction.
class PositionOnlyOperation : public SpaceSwitchOperation {
public:
  PositionOnlyOperation(Axis axis, int64_t position, bool natural)
      : SpaceSwitchOperation(
            axis == Axis::kVertical
                ? std::unique_ptr<AxisAdapter>(
                      std::make_unique<VerticalAxisAdapter>(TestSpaceState()))
                : std::unique_ptr<AxisAdapter>(
                      std::make_unique<HorizontalAxisAdapter>(TestSpaceState())),
            Options{.natural_scrolling_enabled = natural}),
        position_(position) {}
  constexpr absl::string_view debug_name() const override {
    return "PositionOnlyOperation";
  }
private:
  int64_t position_;
  int64_t position_locked() const override { return position_; }
  void SetPositionLocked(int64_t position, CGEventSink*) override {
    position_ = position;
  }
  void CommitLocked(CGEventSink*) override {}
};

struct Configuration {
  int os;
  bool natural;
  Axis axis;
};

}  // namespace

class PhysicalEventHandlerTest : public testing::TestWithParam<Configuration> {
protected:
  std::optional<int> original_os_ = absl::GetFlag(FLAGS_force_os_version);
  std::unique_ptr<PhysicalEventHandler> handler_;
  NoPostSink sink_;

  void TearDown() override {
    handler_.reset();
    absl::SetFlag(&FLAGS_force_os_version, original_os_);
  }

  void SetUpHandler(Configuration config, int64_t initial,
                    std::optional<int64_t> target = std::nullopt,
                    int duration_seconds = 0) {
    handler_.reset();
    absl::SetFlag(&FLAGS_force_os_version, config.os);
    proto::DaemonOptions options;
    options.set_frames_per_second(120);
    options.mutable_vertical_settings()->set_enabled(true);
    options.mutable_horizontal_settings()->set_enabled(true);
    options.mutable_vertical_settings()->mutable_duration()->set_seconds(
        duration_seconds);
    options.mutable_horizontal_settings()->mutable_duration()->set_seconds(
        duration_seconds);
    handler_.reset(new PhysicalEventHandler(options, HotkeyConfigurations{}));
    handler_->current_axis_ = config.axis;
    handler_->initial_position_ = initial;
    handler_->target_position_ = target.value_or(initial);
    handler_->animator_ = std::make_unique<SwipeAnimator>(
        std::make_unique<PositionOnlyOperation>(config.axis, initial,
                                                config.natural));
  }

  DockControlEvent Event(int phase, double progress) const {
    return {.phase = phase,
            .direction = static_cast<int>(handler_->current_axis_),
            .progress = progress};
  }
  void Change(double raw) {
    ASSERT_TRUE(handler_->HandleChangeGesture(Event(kGestureChanged, raw),
                                               &sink_).ok());
  }
  void End(double raw) {
    ASSERT_TRUE(handler_->HandleEndGesture(Event(kGestureEnded, raw)).ok());
  }
  void Cancel(double raw) {
    ASSERT_TRUE(handler_->HandleCancelGesture(Event(kGestureCancelled, raw)).ok());
  }
  int64_t Position() const { return handler_->animator_->position(); }
  int64_t Target() const { return handler_->target_position_; }
  int64_t Delta(double progress) const {
    return handler_->animator_->operation().axis_adapter().ProgressToNanoswipes(progress);
  }
  void WaitForCommit() {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!handler_->animator_->is_committed() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(handler_->animator_->is_committed());
  }
  void Resume() {
    ASSERT_TRUE(handler_->SetUpForNewGesture(handler_->current_axis_).ok());
  }
  int64_t Initial() const { return handler_->initial_position_; }
  void KeyboardDown() {
    ASSERT_TRUE(handler_->HandleCommand(PhysicalEventHandler::RelativeMoveCommand{
        .arrow_key_direction = PhysicalEventHandler::ArrowKeyDirection::kDown}).ok());
  }
};

TEST_P(PhysicalEventHandlerTest, ChangedAndEndedUseSameCoordinates) {
  const auto config = GetParam();
  const bool inverted = config.os >= 27 && config.natural &&
                        config.axis == Axis::kVertical;
  // Both signs: vertical covers both entry routes and both exits. Horizontal
  // uses the real multi-space conversion, not a vertical stand-in.
  for (const int sign : {-1, 1}) {
    for (const bool exiting : {false, true}) {
      const int64_t initial = config.axis == Axis::kVertical
                                  ? (exiting ? -sign * kOneSwipeInNanoswipes : 0)
                                  : kOneSwipeInNanoswipes;
      SetUpHandler(config, initial);
      const double raw = (inverted ? -sign : sign) * 0.329223;
      Change(raw);
      EXPECT_EQ(Position(), initial + Delta(sign * 0.329223));
      End(raw);
      const int64_t expected = initial + sign * kOneSwipeInNanoswipes;
      EXPECT_EQ(Target(), expected);
      WaitForCommit();
      EXPECT_EQ(Position(), expected);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    CoordinateModes, PhysicalEventHandlerTest,
    testing::Values(
        Configuration{26, false, Axis::kVertical},
        Configuration{26, true, Axis::kVertical},
        Configuration{27, false, Axis::kVertical},
        Configuration{27, true, Axis::kVertical},
        Configuration{28, true, Axis::kVertical},
        Configuration{26, false, Axis::kHorizontal},
        Configuration{26, true, Axis::kHorizontal},
        Configuration{27, false, Axis::kHorizontal},
        Configuration{27, true, Axis::kHorizontal}));

TEST_F(PhysicalEventHandlerTest, MissionControlExitTraceMovesTowardDesktop) {
  SetUpHandler({27, true, Axis::kVertical}, 1'000'000);
  // Anonymous numeric fixtures derived from the physical reproduction. Before
  // normalization these move above +1 swipe and end clamped at Mission Control.
  Change(0.044158);
  EXPECT_EQ(Position(), 955'842);
  Change(0.329223);
  EXPECT_EQ(Position(), 670'777);
  End(0.329223);
  EXPECT_EQ(Target(), 0);
  WaitForCommit();
  EXPECT_EQ(Position(), 0);
}

TEST_F(PhysicalEventHandlerTest, CancelReturnsToInitialPositionForBothSigns) {
  for (const int sign : {-1, 1}) {
    SetUpHandler({27, true, Axis::kVertical}, sign * kOneSwipeInNanoswipes);
    Change(sign * 0.25);
    EXPECT_EQ(Position(), sign * 750'000);
    Cancel(sign * 0.25);
    WaitForCommit();
    EXPECT_EQ(Position(), sign * kOneSwipeInNanoswipes);
  }
}

TEST_F(PhysicalEventHandlerTest, ResumeUsesCurrentPositionAndRetainsPendingTarget) {
  for (const int sign : {-1, 1}) {
    SetUpHandler({27, true, Axis::kVertical}, sign * 750'000,
                 sign * kOneSwipeInNanoswipes);
    Resume();
    EXPECT_EQ(Initial(), sign * 750'000);
    EXPECT_EQ(Target(), sign * kOneSwipeInNanoswipes);
    Change(sign * 0.25);
    EXPECT_EQ(Position(), sign * 500'000);
    End(sign * 0.25);
    EXPECT_EQ(Target(), 0);
    WaitForCommit();
    EXPECT_EQ(Position(), 0);
  }
}

TEST_F(PhysicalEventHandlerTest, BeginCancelsActiveAnimationBeforeResuming) {
  SetUpHandler({27, true, Axis::kVertical}, 0, std::nullopt, 60);
  Change(-0.25);
  End(-0.25);
  EXPECT_EQ(Target(), kOneSwipeInNanoswipes);
  Resume();
  const int64_t resumed = Initial();
  EXPECT_GE(resumed, 250'000);
  EXPECT_LT(resumed, kOneSwipeInNanoswipes);
  Change(0.125);
  EXPECT_EQ(Position(), resumed - 125'000);
  // End retargets from the pending +1 boundary, not from the current fraction.
  End(0.125);
  EXPECT_EQ(Target(), 0);
}

TEST_F(PhysicalEventHandlerTest, KeyboardDirectionIsNotNormalized) {
  SetUpHandler({27, true, Axis::kVertical}, kOneSwipeInNanoswipes);
  KeyboardDown();
  EXPECT_EQ(Target(), 0);
  WaitForCommit();
  EXPECT_EQ(Position(), 0);
}

TEST_F(PhysicalEventHandlerTest, ZeroProgressRetainsExistingNegativeEndRule) {
  SetUpHandler({27, true, Axis::kVertical}, kOneSwipeInNanoswipes);
  Change(0);
  EXPECT_EQ(Position(), kOneSwipeInNanoswipes);
  End(0);
  EXPECT_EQ(Target(), 0);
  WaitForCommit();
  EXPECT_EQ(Position(), 0);
}

}  // namespace fasterswiper
