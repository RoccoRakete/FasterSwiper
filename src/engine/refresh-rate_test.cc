#include "src/engine/refresh-rate.h"

#include <gtest/gtest.h>
#include <limits>
#include <vector>

namespace fasterswiper {
namespace {
TEST(RefreshRateTest, ManualDoesNotQueryDisplayAndPreservesAllPositiveTargets) {
  for (int64_t rate : {1LL, 29LL, 60LL, 144LL, 360LL, 1000LL,
                       std::numeric_limits<long long>::max()}) {
    EXPECT_EQ(ResolveAnimationFrameRate(rate, false,
        []() -> std::optional<CGDirectDisplayID> { ADD_FAILURE(); return 1; },
        [](CGDirectDisplayID) { ADD_FAILURE(); return 60; }), rate);
  }
}
TEST(RefreshRateTest, SelectedDisplayAndRateAreReadAgainForEveryAnimation) {
  CGDirectDisplayID selected = 17;
  double rate = 59.94;
  std::vector<CGDirectDisplayID> queried;
  DisplaySelector selector = [&] { return selected; };
  RefreshRateProvider provider = [&](CGDirectDisplayID display) {
    queried.push_back(display); return rate;
  };
  EXPECT_EQ(ResolveAnimationFrameRate(144, true, selector, provider), 60);
  selected = 99; rate = 120;
  EXPECT_EQ(ResolveAnimationFrameRate(144, true, selector, provider), 120);
  rate = 360;
  EXPECT_EQ(ResolveAnimationFrameRate(144, true, selector, provider), 360);
  EXPECT_EQ(queried, (std::vector<CGDirectDisplayID>{17, 99, 99}));
}
TEST(RefreshRateTest, LookupFailureAndInvalidRateFallBackToManual) {
  auto provider = [](CGDirectDisplayID) { ADD_FAILURE(); return 60; };
  EXPECT_EQ(ResolveAnimationFrameRate(73, true,
      []() -> std::optional<CGDirectDisplayID> { return std::nullopt; }, provider), 73);
  EXPECT_EQ(ResolveAnimationFrameRate(73, true, [] { return 0; }, provider), 73);
  for (double rate : {0.0, -60.0, 0.1, std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_EQ(ResolveAnimationFrameRate(73, true, [] { return 1; },
        [rate](CGDirectDisplayID) { return rate; }), 73);
  }
}
TEST(RefreshRateTest, InvalidManualAndExtremeRatesCannotMakeInvalidTimerPeriod) {
  for (int64_t rate : {0LL, -1LL, std::numeric_limits<long long>::min()}) {
    EXPECT_EQ(AnimationPeriodNanoseconds(rate), 4'166'666);
    EXPECT_EQ(ResolveAnimationFrameRate(rate, false, {}, {}), 240);
  }
  EXPECT_EQ(AnimationPeriodNanoseconds(360), 2'777'777);
  EXPECT_EQ(AnimationPeriodNanoseconds(std::numeric_limits<int64_t>::max()), 1);
  EXPECT_EQ(ResolveAnimationFrameRate(144, true, [] { return 1; },
      [](CGDirectDisplayID) { return std::numeric_limits<double>::max(); }), 1'000'000'000);
}
}  // namespace
}  // namespace fasterswiper
