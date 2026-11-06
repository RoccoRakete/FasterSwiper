#include "src/engine/refresh-rate.h"

#include "src/cf-util.h"
#include "src/space-state.h"

#include <ColorSync/ColorSync.h>
#include <CoreVideo/CoreVideo.h>
#include <algorithm>
#include <cmath>

namespace fasterswiper {
namespace {
constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
bool ValidRate(double rate) { return std::isfinite(rate) && rate > 0; }
}  // namespace

std::optional<CGDirectDisplayID> SelectCursorDisplay() {
  // LoadSpaceState selects a managed display under the cursor, including the
  // existing ordering for mirrored/overlapping displays. Do not use main screen.
  auto state = LoadSpaceStateForActiveDisplay();
  if (!state.ok()) return std::nullopt;
  auto uuid = WrapCFUnique(CFUUIDCreateFromString(nullptr, state->display_id().get()));
  if (!uuid) return std::nullopt;
  const CGDirectDisplayID display = CGDisplayGetDisplayIDFromUUID(uuid.get());
  if (display == kCGNullDirectDisplay) return std::nullopt;
  return display;
}

double ReadDisplayRefreshRate(CGDirectDisplayID display) {
  if (display == kCGNullDirectDisplay) return 0;
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
  if (mode != nullptr) {
    const double rate = CGDisplayModeGetRefreshRate(mode);
    CGDisplayModeRelease(mode);
    if (ValidRate(rate)) return rate;
  }

  // CoreVideo's nominal query is deprecated since macOS 15. This fallback is
  // deliberately scoped: no callback, Start(), run loop, AppKit, or main-thread
  // dispatch. It queries the ideal period, NOT instantaneous VRR or vsync.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  CVDisplayLinkRef link = nullptr;
  const CVReturn result = CVDisplayLinkCreateWithCGDisplay(display, &link);
  if (result != kCVReturnSuccess || link == nullptr) {
    if (link != nullptr) CVDisplayLinkRelease(link);
    return 0;
  }
  const CVTime period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
  CVDisplayLinkRelease(link);
#pragma clang diagnostic pop
  if ((period.flags & kCVTimeIsIndefinite) != 0 || period.timeValue <= 0 ||
      period.timeScale <= 0) return 0;
  const double rate = static_cast<double>(period.timeScale) / period.timeValue;
  return ValidRate(rate) ? rate : 0;
}

int64_t ResolveAnimationFrameRate(int64_t manual, bool match_screen,
                                 const DisplaySelector& select_display,
                                 const RefreshRateProvider& refresh_rate) {
  const int64_t fallback = manual > 0 ? manual : 240;
  if (!match_screen) return fallback;
  const auto display = select_display();
  if (!display || *display == kCGNullDirectDisplay) return fallback;
  const double rate = refresh_rate(*display);
  if (!ValidRate(rate)) return fallback;
  // The animator API takes whole FPS: 59.94 becomes 60. The sole upper bound
  // corresponds to its 1 ns timer resolution, not an arbitrary 240 FPS cap.
  const double rounded = std::round(rate);
  if (rounded < 1) return fallback;
  return static_cast<int64_t>(std::min(rounded,
                                     static_cast<double>(kNanosecondsPerSecond)));
}

int64_t AnimationPeriodNanoseconds(int64_t rate) {
  return std::max<int64_t>(1, kNanosecondsPerSecond / (rate > 0 ? rate : 240));
}

}  // namespace fasterswiper
