#pragma once

#include <CoreGraphics/CoreGraphics.h>
#include <cstdint>
#include <functional>
#include <optional>

namespace fasterswiper {

using DisplaySelector = std::function<std::optional<CGDirectDisplayID>()>;
using RefreshRateProvider = std::function<double(CGDirectDisplayID)>;

// Same managed-display selection as space-state, without AppKit/main dispatch.
std::optional<CGDirectDisplayID> SelectCursorDisplay();
// Positive mode rate first, then a short-lived, never-started nominal display link.
double ReadDisplayRefreshRate(CGDirectDisplayID display);

// Invalid/unknown automatic rates fall back to the retained manual target.
int64_t ResolveAnimationFrameRate(int64_t manual, bool match_screen,
                                 const DisplaySelector& select_display,
                                 const RefreshRateProvider& refresh_rate);
// A positive integer rate always produces a nonzero period, even above 1 GHz.
int64_t AnimationPeriodNanoseconds(int64_t rate);

}  // namespace fasterswiper
