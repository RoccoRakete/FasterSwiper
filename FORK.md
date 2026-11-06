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

Only direct numbered-space shortcuts gain a configurable modifier. Physical
swipes, animation settings and Mission Control shortcut lookup retain upstream
behavior. The keyboard settings picker offers **Command (⌘)** and
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

## Verification and remaining acceptance

The local full application build succeeds. All 5 available Bazel test targets
were genuinely rerun: the final run passed 30 individual test cases with no
failures or errors. One earlier rerun exposed an upstream live-state dependency:
`AxisAdapterTest.VerticalAxisAdapter_DesktopSpace` expected 0 but observed the
Mission Control position 1,000,000. Its production method queries the active
WindowServer state, despite the test supplying a synthetic desktop SpaceState.
A subsequent uncached full rerun passed without changing code or controlling
the desktop. Run these upstream tests while the desktop, rather than Mission
Control/App Exposé, is active. The failure log is retained only as local build
evidence and is not included in the published source; no flaky-test suppression
or production gesture changes were introduced.

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
