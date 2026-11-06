# FasterSwiper fork

## Provenance and scope

This fork is based on `mgbowen/FasterSwiper` commit
`3665350bbd05cb7245063ade5143f155d8228d27`. The upstream Apache-2.0
`LICENSE` and `ATTRIBUTION.md` are preserved and included as app resources.
The fork repository is `RoccoRakete/FasterSwiper`.

The app is named **FasterSwiper**, with bundle identifier
`com.martin.FasterSwiper.Command`. Internal library, module and Bazel target names
remain unchanged. Version numbers are unchanged. There is no updater.
The display-name change retains this fork's bundle identifier and preferences
domain; it does not migrate settings or replace any installed or working copy.

Direct numbered-space shortcuts gain a configurable modifier. Outgoing vertical
gestures also receive the macOS 27 natural-scrolling coordinate correction described
below. Other physical swipe behavior, duration/easing settings and Mission Control
shortcut lookup retain upstream behavior. The keyboard settings picker offers **Command (⌘)** and
**Control (⌃)**. Command is the default. The selected value is stored as field 9
(`jump_to_space_modifier`) of the existing serialized daemon options. Missing values, including saved
options predating this field, hydrate to Command. Explicit Control values survive
hydration and serialization. Existing enable/disable settings are retained.
Changing the picker uses the existing save and debounced daemon-restart path.

The fork uses its own normal `UserDefaults` bundle domain. It does not read from
or migrate the original `com.mgbowen.FasterSwiper` domain. Reusing a bundle ID
alone does not guarantee macOS permission continuity with a previous binary.

## Shortcut behavior and conflicts

- Command+1 through Command+0 target spaces 1 through 10 on the active display.
  Select Control to use Control+1 through Control+0 instead.
- Both jump-to-space shortcuts and horizontal animations must be enabled.
  Disabling either leaves these keys to macOS/apps. The picker keeps its saved
  selection while disabled.
- Only the selected modifier matches. Additional Command, Control, Option, Shift
  or Caps Lock modifiers prevent a match, preserving upstream exact-mask behavior.
  The physical top-row number key mapping is unchanged; the numeric keypad is
  not supported. Key-up events are not consumed by this matcher.
- While enabled, matching Command-number key-downs take precedence over app
  commands, such as selecting browser tabs. As upstream, a recognized shortcut
  can be consumed even when the requested space does not exist. Select Control
  or disable numbered-space shortcuts if you need those app commands.
- The modifier picker does not remap the separate Mission Control/arrow
  shortcuts. These continue to use the existing System Settings configuration
  and interception toggle.

## Physical vertical swipe coordinates

The first attempted fix (`01bd121`) negated incoming vertical progress on macOS
27 with natural scrolling. It passed position-only unit tests but **failed actual
physical-trackpad acceptance**. Those tests did not inspect generated events or
the state reached by their emitted signs, so they were insufficient evidence.

Correlating the before/after diagnostic sessions exposed the outgoing mismatch:

| Implementation | Raw vertical sign from desktop | Internal target | Next detected state |
| --- | --- | --- | --- |
| Before incoming normalization | positive | +1 | -1 (App Exposé) |
| Before incoming normalization | negative | -1 | +1 (Mission Control) |
| First attempted fix | positive | -1 | +1 (Mission Control) |
| First attempted fix | negative | +1 | -1 (App Exposé) |

Positions in this table use whole swipes. Mission Control is +1, desktop is 0,
and App Exposé is -1. The natural-scrolling adaptation in the outgoing event
factory was reversing vertical progress and velocity as well as horizontal
output. Incoming normalization hid the entry symptom while leaving outgoing
state opposite to the internal target. A subsequent exit from detected Mission
Control with negative raw progress was inverted upward and clamped at +1.

The second fix removes incoming normalization entirely. In
`CreateDockControlGestureEvent`, the macOS 27-or-later natural-scrolling sign
adaptation now applies **only to horizontal events**. Vertical outgoing progress
and velocity keep canonical +Mission Control / -App Exposé signs, consistent
with the existing natural-scrolling-off keyboard path. Horizontal output,
macOS 26 behavior and natural-scrolling-off behavior are unchanged.

Generated-event tests create real CGEvents without posting them and decode
progress, both velocity fields, phase and axis. They cover both axes, both
signs and zero, present/absent velocity, all four phases, natural scrolling on/off,
and forced macOS 26/27/28 compatibility modes. CGEvent fields round through
single precision, so decoded numeric comparisons use float precision.

Integration tests run the real begin/change/end/cancel handlers, animator,
segmented operation, event factory and augmentation. Only external state is
replaced by a vertical adapter subclass and a memory-only sink. The terminal
state model reads **emitted** ended-event signs, not the handler target. It
covers both entries and exits, including anonymized raw progress fixtures:
Mission Control +1 with -0.406433 returns to 0; App Exposé -1 with +0.354965
returns to 0; desktop +0.333588 enters +1 and -0.540634 enters -1. This model is
an inference from correlated traces, not a live WindowServer acceptance test.
No raw logs, display IDs or private diagnostic paths are included in source.

End/cancel handlers accept an owned sink, defaulting to their unchanged
production posting sink. Tests inject memory-only sinks through completion and
explicitly commit cleanup before animator destruction. Test-executable-local
guards replace both macOS posting APIs and fail if unexpectedly selected. A
regression test exercises both production sink classes against these guards,
verifying no live input is sent. Position-only tests remain useful for pending
targets, interruption/resume, zero-progress and keyboard direction, but are no
longer the sole regression evidence.

These deterministic tests do not establish physical acceptance. Real trackpad
entry and exit in Mission Control/App Exposé still require user verification
after a separately authorized installation. This second-fix build is not
installed or launched by implementation/build/test.

## Tested local build without installation or launch

Prerequisites: Apple Silicon macOS, full Xcode at `/Applications/Xcode.app`,
network access for the first build, `curl`, and `python3` for preserving test
reports. The app's deployment target remains macOS 26.0.
This path was exercised with Xcode 27.2 (27B52) and Bazel 9.2.0.

```bash
./tools/build-command.sh
```

The helper downloads the pinned ARM64 Bazel binary to `.local/tools/bazel` and
checks SHA256 `dd466352a3e4d3581b8898740ee1ff208866ccbe25f8d367c5dcb950219587e6`.
All Bazel dependencies and build caches stay under `.local/bazel`. The repository
contents cache is disabled because Bazel rejects placing it inside the source
checkout. No Homebrew/global package installation is needed. The script sets
`DEVELOPER_DIR` per process and never changes `xcode-select`.

The exact build and test invocations used by the helper are:

```bash
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
.local/tools/bazel --output_user_root="$PWD/.local/bazel" test \
  -c opt --repo_contents_cache= --jobs=6 --build_tests_only \
  --test_output=errors //src/...
.local/tools/bazel --output_user_root="$PWD/.local/bazel" build \
  -c opt --repo_contents_cache= --jobs=6 //src/app:FasterSwiper_app
```

To force a genuine rerun instead of using cached test results, add
`--nocache_test_results` to the test invocation. `--build_tests_only` avoids
compiling unrelated developer tools under `src/tools`.

Outputs, all ignored by Git:

- `artifacts/FasterSwiper.app`
- `artifacts/FasterSwiper-source.zip`
- `artifacts/SHA256SUMS` (ZIP and executable checksums)
- Preserved XML and text test reports and case counts in `artifacts/test-results/`

`tools/export-test-results.py` preserves the test reports before the app build
changes Bazel's output symlinks. The original reports remain in the Bazel cache;
the `bazel-testlogs` convenience symlink may point at a different configuration
after building the app.

The helper only replaces its own build/test outputs in this repository's
`artifacts` directory. It never installs, starts, stops or removes an application, changes
preferences, registers login items or grants permissions.

## Signing and permissions boundary

The app target sets the rules_apple `disable_legacy_signing` feature. No bundle
`codesign` operation, Developer ID certificate, provisioning profile,
notarization upload or debug entitlement is requested by this fork's build.
Apple Silicon's linker still embeds an **ad-hoc, linker-signed Mach-O signature**.
On the tested output `codesign -dv --verbose=4` reports
`flags=0x20002(adhoc,linker-signed)`, `TeamIdentifier=not set`,
`Info.plist=not bound` and `Sealed Resources=none`. This is not a Developer ID
signed, hardened-runtime or notarized distribution.

On the observed unsigned bundle, `codesign --verify --deep --strict --verbose=4`
exits with status 1 and the exact diagnostic
`code has no resources but signature indicates they must be present`.
The linker signature is not a valid bundle resource seal; a successful build is
not a successful signature verification. No signing or signature-setting workaround
has been applied. The decision whether and how to sign is deferred to the user.
This build has no runtime approval: it has not been launched or interactively
accepted, and macOS permissions have not been verified for it.

Source changes and a successful build do not resolve Gatekeeper, accessibility,
Device Control/Data Access or other macOS authorization requirements. No SIP,
quarantine, Gatekeeper, TCC or library-validation settings are modified. Any
manual first launch and permission checks remain a separate acceptance step.
Do not run the original and fork together. Keep the original and prior working
copy until the new app has passed real interactive acceptance.

## Bundled app icon

The fork's original artwork is a blue rounded-square background, overlapping
windows and a white swipe arrow. No external images, fonts or SF Symbols were
used to make it. The menu-bar SF Symbol is unchanged. The app name, technical
bundle identifier and deployment target are unchanged.

Regenerate the checked-in artwork with Apple's SDK and Swift, without installing
packages or opening an app:

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  CLANG_MODULE_CACHE_PATH="$PWD/.local/icon-module-cache" \
  xcrun swift tools/generate-app-icon.swift
```

This creates ten PNG assets (16 through 1024 pixels) and `Contents.json` in
`src/app/Assets.xcassets/AppIcon.appiconset`, plus the native
`src/app/AppIcon.icon` source. Regeneration was checked byte-for-byte on the
local SDK. The source PNG catalog is retained for reference/editing; the build
uses the native `.icon` bundle through `macos_application.app_icons`.

The pinned rules_apple rejects legacy `.appiconset` inputs for deployment targets
26.0 or newer (`apple/internal/resource_actions/actool.bzl`,
`_validate_standard_app_icon_sets`). It was inspected and the real failure was
confirmed before switching to the native format, rather than changing the
minimum OS version or patching a cached dependency. The app_icons API is in
`apple/internal/rule_attrs.bzl`, `_app_icon_attrs`. Apple's
[Icon Composer documentation](https://developer.apple.com/documentation/xcode/creating-your-app-icon-using-icon-composer)
describes native icon sources and generated size/appearance variants.

The successful app build contains `CFBundleIconFile=AppIcon`,
`CFBundleIconName=AppIcon`, `Contents/Resources/AppIcon.icns` and `Assets.car`.
Xcode's native pipeline emits 16/32/128/256-pixel compatibility ICNS images;
`Assets.car` contains the complete multi-size icon (16/32/64/128/256/512/1024)
and 1024-pixel native layers. `tools/verify-app-package.py` verifies the actual
exported app and ZIP match, identity, license resources, ICNS header/chunks,
real `iconutil` decode, PNG CRC/decompression and `sips` dimensions, and
`assetutil --info` catalog renditions. The build helper runs this automatically.
It does not launch the app or mutate Finder's icon cache. Finder display after
installation remains a human acceptance check.

## Frame-rate selection

Animation settings offer **Manual** and **Match screen refresh rate**. Manual
remains the default, including for existing preferences. Field 10
(`match_screen_refresh_rate`) is an explicitly present boolean, hydrated to
false when absent. Field 4 (`frames_per_second`) remains the retained manual
target/fallback; toggling matching never replaces it. The Swift protobuf property
is generated from this schema and used by the actual settings view model through
the existing serialized SettingsStore save and debounced restart path. The manual
field is disabled while matching is active. The settings caption explains the
fallback and ProMotion limitation; no misleading live/effective-rate readout is
shown.

The engine resolves a rate at **each** end-gesture, cancel-gesture, relative
keyboard move and numbered-space jump animation start. It uses the same managed
display-under-cursor selection as space-state, converts its UUID to the Core
Graphics display ID, and queries that display, not the main display. A display
or mode change between animations is therefore re-read. These C/C++ queries do
not use NSScreen/AppKit, synchronize with the main thread, or add an observer,
helper service or continuous display-link loop.

A finite positive `CGDisplayModeGetRefreshRate(CGDisplayCopyDisplayMode(id))`
result wins. If unavailable, a short-lived `CVDisplayLinkCreateWithCGDisplay`
instance is queried with `CVDisplayLinkGetNominalOutputVideoRefreshPeriod` and
released; it is **never started**, and no callback is registered. CoreVideo's
legacy display-link API is deprecated from macOS 15. The fallback deliberately
uses it only to obtain a nominal period without a GUI-thread dispatch or running
display-link service. Invalid, indefinite or nonpositive periods, invalid rates
and display lookup failures fall back to the saved manual rate. Invalid manual
values use 240 for execution without rewriting stored data. All positive manual
values are preserved, including values below 30 or above 240. Automatic rates
such as 360 are not capped at 240. The integer animator API rounds fractional
rates (59.94 becomes 60); only its 1-nanosecond timer resolution limits the
resolved rate/period, avoiding zero periods, division by zero and integer
conversion overflow.

This is a nominal animation-timer target, **not** live ProMotion/VRR tracking,
maximum-panel-rate detection, guaranteed frame delivery or vsync alignment. In
particular, it does not promise 120 FPS on every ProMotion mode. A successful
build and deterministic tests are not evidence of physical display timing.

Primary API references checked against Apple documentation and the local SDK:

- [CGDisplayMode.refreshRate / CGDisplayModeGetRefreshRate](https://developer.apple.com/documentation/coregraphics/cgdisplaymode/refreshrate),
  `CoreGraphics.framework/Headers/CGDirectDisplay.h`: the rate of the selected mode.
- [CVDisplayLinkGetNominalOutputVideoRefreshPeriod](https://developer.apple.com/documentation/corevideo/cvdisplaylinkgetnominaloutputvideorefreshperiod(_:)),
  `CoreVideo.framework/Headers/CVDisplayLink.h`: the ideal/nominal period, which
  may be indefinite; distinct from the measured actual-period API.
- [CVDisplayLinkCreateWithCGDisplay](https://developer.apple.com/documentation/corevideo/cvdisplaylinkcreatewithcgdisplay(_:_:)),
  `CVDisplayLink.h`: creates for the specified display. The same header marks
  the API family deprecated from macOS 15 and declares Start separately.
- `CoreVideo.framework/Headers/CVBase.h`: `CVTime` has a signed 64-bit timeValue,
  signed 32-bit timeScale and `kCVTimeIsIndefinite` flag.
- `ColorSync.framework/Headers/ColorSyncDevice.h`: declares
  `CGDisplayGetDisplayIDFromUUID` and its inverse used by space-state.

Headers are under
`/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/`.
Retrieved Apple documentation JSON is retained only in ignored local build evidence.

## Icon / refresh-rate verification

All 8 test targets were rerun uncached: **59 cases pass** (the prior 51 plus
8 new cases). New tests cover old-preference migration to manual, repeated
serialization/hydration while toggling modes, preservation of manual and other
settings, screen IDs 17/99 and changing providers, fractional/360-Hz rates,
invalid/missing display/rate fallbacks, and safe periods for extreme inputs.
The real four handler paths are exercised against a no-post operation and the
existing executable-local guards, checking the actual configured animator timer
period at 60 Hz, 360 Hz and manual fallback. The existing emitted-event vertical
regressions and shortcut tests remain intact. A mutation reverting all four
call sites to the manual target failed the new integration test on **every**
path; restoration returned the full suite to green. Reports are in ignored
`artifacts/test-results`, with the uncached run and negative mutation logs nearby.
The actual SwiftUI view/model and generated Swift proto bridge compile in the
full app build. No test reads or writes the installed app's preferences, launches
the app, or posts live input.

Before rebuilding, the accepted exported app/ZIP/reports were preserved in
ignored `artifacts/rollback-pre-icon-refresh/`. The new app and ZIP remain under
`artifacts/`; no installation, launch, stop, login-item, permission, signing,
commit or push operation is part of this change. The existing linker-only
signature limitation above remains. Human acceptance must still verify Finder's
icon and settings persistence across an actual authorized relaunch, automatic
matching across physical displays/modes, and gesture behavior.

## Verification and remaining acceptance

The second-fix local full application build succeeds. All 7 Bazel test targets
were genuinely rerun: 51 individual test cases passed without failures or errors,
including 19 handler/guard cases and 2 generated-event cases. Before changing
production coordinates, the corrected regression run against the first fix's
incoming/outgoing behavior passed 9 of 17 handler cases and failed 8; the event
suite passed its horizontal case and failed its vertical case. This red run is
retained locally alongside the green reports. Two further handler/guard cases
were added afterward. A separate output-only mutation restored the incorrect
vertical output flip while retaining corrected input: 3 integration cases and
the generated vertical-event case failed, while all position-only cases and
the horizontal-event case passed. Restoring horizontal-only adaptation returned
the full suite to green. This verifies that integration coverage detects the
outgoing defect independently of incoming normalization.

One earlier rerun exposed an upstream live-state dependency:
`AxisAdapterTest.VerticalAxisAdapter_DesktopSpace` expected 0 but observed the
Mission Control position 1,000,000. Its production method queries the active
WindowServer state, despite the test supplying a synthetic desktop SpaceState.
A subsequent uncached full rerun passed without changing code or controlling
the desktop. Run these upstream tests while the desktop, rather than Mission
Control/App Exposé, is active. The failure log is retained only as local build
evidence and is not included in the published source; no flaky-test suppression
or desktop control was introduced to obtain a passing run.

The new matcher has 6 regression cases covering all ten keys in both
modes, missing-field defaults, inactive/additional modifiers, key-up and unrelated
keys, disabled settings and unchanged arrow matching. Public C API tests cover
the default, hydration, preservation of existing disabled settings and modifier
serialization round trips. Existing channel, compatibility and axis-adapter tests
also pass. SwiftUI views and the actual settings view model compile in the full
app build; these are not interactive UI tests.

The app has deliberately **not been launched** during implementation. Review,
interactive Command/Control switching, settings persistence across an actual
relaunch, permissions, gesture behavior and eventual retirement of older copies
remain acceptance work. No commit, push, release or version bump is part of this
local implementation.
