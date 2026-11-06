#include "src/engine/physical-event-handler.h"

#include "src/cf-util.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"

#include <chrono>
#include <thread>
#include <vector>
#include <mutex>

#include "src/event.h"

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

// Fail closed even if a future regression accidentally selects a live sink.
// Executable-local definitions resolve both production post APIs to these guards.
extern "C" void CGEventPost(CGEventTapLocation, CGEventRef) {
  ADD_FAILURE() << "Live CGEventPost blocked by test";
}
extern "C" void CGEventTapPostEvent(CGEventTapProxy, CGEventRef) {
  ADD_FAILURE() << "Live CGEventTapPostEvent blocked by test";
}

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

struct RecordedEvent {
  int phase;
  double progress;
  double velocity;
};
struct Recording {
  std::mutex mutex;
  std::vector<RecordedEvent> events;
  int64_t detected_position = 0;
};

// This sink only decodes events in memory. It never calls either macOS post API.
class RecordingSink : public CGEventSink {
 public:
  explicit RecordingSink(std::shared_ptr<Recording> recording)
      : recording_(std::move(recording)) {}
  void Post(CGEventRef event) override {
    std::lock_guard lock(recording_->mutex);
    const int phase = CGEventGetIntegerValueField(event, kCGEventGesturePhase);
    const double progress = CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress);
    recording_->events.push_back({phase, progress,
        CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY)});
    // Evidence-backed terminal model: positive outgoing vertical progress moves
    // toward MC (+1); negative toward App Expose (-1), regardless of natural mode.
    // This deliberately uses emitted fields, never the operation's target.
    if (phase == kGestureEnded) {
      recording_->detected_position += progress > 0 ? kOneSwipeInNanoswipes
                                                    : -kOneSwipeInNanoswipes;
    }
  }
 private:
  std::shared_ptr<Recording> recording_;
};

// Only external state is replaced. Conversions, limits, segmented operation,
// event augmentation, handler and animation are production implementations.
class RecordedVerticalAxis : public VerticalAxisAdapter {
 public:
  explicit RecordedVerticalAxis(std::shared_ptr<Recording> recording)
      : VerticalAxisAdapter(TestSpaceState()), recording_(std::move(recording)) {}
  absl::StatusOr<int64_t> committed_position() const override {
    std::lock_guard lock(recording_->mutex);
    return recording_->detected_position;
  }
  bool WaitForCommittedPositionChanged(int64_t original, absl::Duration) const override {
    return *committed_position() != original;
  }
 private:
  std::shared_ptr<Recording> recording_;
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
  std::shared_ptr<Recording> recording_;
  SpaceSwitchOperation* recorded_operation_ = nullptr;

  void ClearHandler() {
    if (recorded_operation_) {
      (void)handler_->animator_->CancelAnimation();
      RecordingSink sink(recording_);
      recorded_operation_->Commit(&sink);
      recorded_operation_ = nullptr;
    }
    handler_.reset();
    recording_.reset();
  }

  void TearDown() override {
    ClearHandler();
    absl::SetFlag(&FLAGS_force_os_version, original_os_);
  }

  void SetUpHandler(Configuration config, int64_t initial,
                    std::optional<int64_t> target = std::nullopt,
                    int duration_seconds = 0) {
    ClearHandler();
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
    ASSERT_TRUE(handler_->HandleEndGesture(Event(kGestureEnded, raw),
        recording_ ? std::unique_ptr<CGEventSink>(std::make_unique<RecordingSink>(recording_))
                   : std::unique_ptr<CGEventSink>(std::make_unique<NoPostSink>())).ok());
  }
  void Cancel(double raw) {
    ASSERT_TRUE(handler_->HandleCancelGesture(Event(kGestureCancelled, raw),
        recording_ ? std::unique_ptr<CGEventSink>(std::make_unique<RecordingSink>(recording_))
                   : std::unique_ptr<CGEventSink>(std::make_unique<NoPostSink>())).ok());
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

  void SetUpIntegration(int os, bool natural, int64_t initial) {
    SetUpHandler({os, natural, Axis::kVertical}, initial);
    recording_ = std::make_shared<Recording>();
    recording_->detected_position = initial;
    auto result = SegmentedSpaceSwitchOperation::Create(
        std::make_unique<RecordedVerticalAxis>(recording_),
        {.natural_scrolling_enabled = natural});
    ASSERT_TRUE(result.ok());
    recorded_operation_ = result->get();
    handler_->animator_ = std::make_unique<SwipeAnimator>(std::move(*result));
    RecordingSink sink(recording_);
    ASSERT_TRUE(handler_->HandleBeginGesture(Event(kGestureBegan, 0), &sink).ok());
  }
  void RecordedChange(double raw) {
    RecordingSink sink(recording_);
    ASSERT_TRUE(handler_->HandleChangeGesture(Event(kGestureChanged, raw), &sink).ok());
  }
};

TEST_P(PhysicalEventHandlerTest, ChangedAndEndedUseSameCoordinates) {
  const auto config = GetParam();
  // Both signs: vertical covers both entry routes and both exits. Horizontal
  // uses the real multi-space conversion, not a vertical stand-in.
  for (const int sign : {-1, 1}) {
    for (const bool exiting : {false, true}) {
      const int64_t initial = config.axis == Axis::kVertical
                                  ? (exiting ? -sign * kOneSwipeInNanoswipes : 0)
                                  : kOneSwipeInNanoswipes;
      SetUpHandler(config, initial);
      const double raw = sign * 0.329223;
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
  // Exit from detected Mission Control: negative raw progress moves toward 0.
  Change(-0.044158);
  EXPECT_EQ(Position(), 955'842);
  Change(-0.329223);
  EXPECT_EQ(Position(), 670'777);
  End(-0.329223);
  EXPECT_EQ(Target(), 0);
  WaitForCommit();
  EXPECT_EQ(Position(), 0);
}

TEST_F(PhysicalEventHandlerTest, CancelReturnsToInitialPositionForBothSigns) {
  for (const int sign : {-1, 1}) {
    SetUpHandler({27, true, Axis::kVertical}, sign * kOneSwipeInNanoswipes);
    Change(-sign * 0.25);
    EXPECT_EQ(Position(), sign * 750'000);
    Cancel(-sign * 0.25);
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
    Change(-sign * 0.25);
    EXPECT_EQ(Position(), sign * 500'000);
    End(-sign * 0.25);
    EXPECT_EQ(Target(), 0);
    WaitForCommit();
    EXPECT_EQ(Position(), 0);
  }
}

TEST_F(PhysicalEventHandlerTest, BeginCancelsActiveAnimationBeforeResuming) {
  SetUpHandler({27, true, Axis::kVertical}, 0, std::nullopt, 60);
  Change(0.25);
  End(0.25);
  EXPECT_EQ(Target(), kOneSwipeInNanoswipes);
  Resume();
  const int64_t resumed = Initial();
  EXPECT_GE(resumed, 250'000);
  EXPECT_LT(resumed, kOneSwipeInNanoswipes);
  Change(-0.125);
  EXPECT_EQ(Position(), resumed - 125'000);
  // End retargets from the pending +1 boundary, not from the current fraction.
  End(-0.125);
  EXPECT_EQ(Target(), 0);
}

TEST_F(PhysicalEventHandlerTest, KeyboardDirectionRetainsCanonicalCoordinates) {
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

TEST_F(PhysicalEventHandlerTest, GeneratedVerticalEntryAndExitMatchDetectedTerminalState) {
  for (int os : {26, 27, 28}) {
    for (bool natural : {false, true}) {
      for (int sign : {-1, 1}) {
        for (bool exiting : {false, true}) {
          SCOPED_TRACE(testing::Message() << os << " " << natural << " " << sign << " " << exiting);
          const int64_t initial = exiting ? -sign * kOneSwipeInNanoswipes : 0;
          SetUpIntegration(os, natural, initial);
          RecordedChange(sign * 0.044158);
          EXPECT_EQ(Position(), initial + sign * 44'158);
          RecordedChange(sign * 0.329223);
          EXPECT_EQ(Position(), initial + sign * 329'223);
          End(sign * 0.329223);
          WaitForCommit();
          const int64_t expected = initial + sign * kOneSwipeInNanoswipes;
          EXPECT_EQ(Target(), expected);
          EXPECT_EQ(Position(), expected);
          std::lock_guard lock(recording_->mutex);
          EXPECT_EQ(recording_->detected_position, expected);
          ASSERT_EQ(recording_->events.size(), 5);
          EXPECT_EQ(recording_->events.front().phase, kGestureBegan);
          EXPECT_EQ(recording_->events.back().phase, kGestureEnded);
          EXPECT_FLOAT_EQ(recording_->events[1].progress, sign * 0.044158);
          EXPECT_FLOAT_EQ(recording_->events[2].progress, sign * 0.329223);
          for (const auto& event : recording_->events) {
            EXPECT_GT(sign * event.progress, 0);
            if (event.phase == kGestureEnded) EXPECT_GT(sign * event.velocity, 0);
          }
        }
      }
    }
  }
}

TEST_F(PhysicalEventHandlerTest, GeneratedCancellationReturnsToDetectedOrigin) {
  for (int sign : {-1, 1}) {
    SetUpIntegration(27, true, sign * kOneSwipeInNanoswipes);
    RecordedChange(-sign * 0.25);
    Cancel(-sign * 0.25);
    WaitForCommit();
    EXPECT_EQ(Position(), sign * kOneSwipeInNanoswipes);
    std::lock_guard lock(recording_->mutex);
    EXPECT_EQ(recording_->detected_position, sign * kOneSwipeInNanoswipes);
    ASSERT_FALSE(recording_->events.empty());
    EXPECT_EQ(recording_->events.back().phase, kGestureCancelled);
    EXPECT_GT(sign * recording_->events.back().progress, 0);
    EXPECT_GT(sign * recording_->events.back().velocity, 0);
  }
}

TEST_F(PhysicalEventHandlerTest, CorrelatedTraceSignsReachCanonicalTerminalState) {
  struct Fixture { int64_t initial; double raw; int64_t expected; };
  // Anonymized ended-progress / detected-position pairs from correlated traces.
  for (const auto fixture : {Fixture{0, 0.333588, 1'000'000},
                             Fixture{0, -0.540634, -1'000'000},
                             Fixture{1'000'000, -0.406433, 0},
                             Fixture{-1'000'000, 0.354965, 0}}) {
    SetUpIntegration(27, true, fixture.initial);
    RecordedChange(fixture.raw);
    EXPECT_EQ(Position(), fixture.initial + static_cast<int64_t>(fixture.raw * kOneSwipeInNanoswipes));
    End(fixture.raw);
    WaitForCommit();
    EXPECT_EQ(Target(), fixture.expected);
    EXPECT_EQ(Position(), fixture.expected);
    std::lock_guard lock(recording_->mutex);
    EXPECT_EQ(recording_->detected_position, fixture.expected);
    ASSERT_FALSE(recording_->events.empty());
    EXPECT_EQ(recording_->events.back().phase, kGestureEnded);
    EXPECT_GT(recording_->events.back().progress * fixture.raw, 0);
  }
}

TEST(NoLiveInputTest, ProductionPostSinksResolveToFailClosedGuards) {
  auto event = WrapCFUnique(CGEventCreate(nullptr));
  ASSERT_NE(event.get(), nullptr);
  EXPECT_NONFATAL_FAILURE(CGEventPostSink().Post(event.get()), "Live CGEventPost blocked");
  int sentinel;
  CGEventTapPostEventSink tap(reinterpret_cast<CGEventTapProxy>(&sentinel));
  EXPECT_NONFATAL_FAILURE(tap.Post(event.get()), "Live CGEventTapPostEvent blocked");
}

}  // namespace fasterswiper
