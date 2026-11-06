# FasterSwiper

A source fork of [mgbowen/FasterSwiper](https://github.com/mgbowen/FasterSwiper)
with its own app identity and a configurable numbered-space shortcut modifier:
**Command by default, Control selectable in Keyboard settings**. See [FORK.md](FORK.md)
for the pinned upstream commit, exact behavior, local build instructions, test
results and the signing/permissions boundary. Upstream license and attribution
are preserved.

The fork includes original bundled app artwork and **Animation → Display &
Performance → Match screen refresh rate**. Manual mode remains the default and
its saved target is retained when automatic matching is selected. Matching uses
the cursor display's nominal rate at each animation start, not instantaneous
ProMotion/VRR tracking or vsync synchronization. An unavailable rate falls back
to the retained manual target. See [FORK.md](FORK.md#frame-rate-selection) for the
API, fallback, and testing details.

A small utility for macOS that allows one to customize the type and speed of
swiping animations when moving between spaces.

This project is inspired by and builds upon the excellent work of
[InstantSpaceSwitcher](https://github.com/jurplel/InstantSpaceSwitcher).

## Demo

### Basic three finger swipes

This video shows how basic three finger swipes are sped up vs. the stock macOS
animations. This was captured with animation duration set to 200ms with a quadratic
ease out.

https://github.com/user-attachments/assets/b55382af-fc5b-4c53-bd92-e321b3a22b64

### Jump to spaces

You can also jump directly to specific spaces using Command+\<space number\>.
Control is selectable in Keyboard settings. Command-number shortcuts conflict
with app shortcuts, for example browser tab selection; select Control or disable
jump-to-space shortcuts if you need those app commands.

https://github.com/user-attachments/assets/29fc5d4c-aa4b-4233-bc74-f6e3110b75fb

### Custom cubic Bezier curves

If the included animation options don't exactly meet your needs, you can set a
custom cubic Bezier curve to get the exact easing you want. It also lets you use
some _interesting_ animations, like so:

https://github.com/user-attachments/assets/d5b5c1ec-cbd2-4908-83b0-29257a692570

## Compatibility

The following assumes the latest publicly released, non-beta version.

### macOS 26 Tahoe

Fully working.

### macOS 27 Golden Gate

Basic functionality is fully working, but there are some bugs [specific to
macOS 27](https://github.com/mgbowen/FasterSwiper/issues?q=is%3Aissue+state%3Aopen+label%3Abug-macos27).

### Other versions

Untested.

## FAQ

### Are any special permissions required to run this?

The fork still requires macOS permissions. Use the permission controls macOS
presents for `FasterSwiper.app` (Accessibility, or Device Control/Data
Access on supported macOS versions). A source build and matching bundle ID do
not guarantee that permissions from an earlier build carry over. This local
build does not grant permissions or bypass platform security; see [FORK.md](FORK.md).

### How does this work?

FasterSwiper tracks physical three-finger gestures, used to move between macOS
spaces. When a gesture completes, it determines which space to move to, then
injects synthetic gestures over a configurable period of time to animate the
movement between spaces.

### Why was this made?

When running on a display at 60Hz, the animation to move between spaces completes
in roughly ½ second, which, while a bit long for my taste, is tolerable. For some
strange reason, the duration doubles when running at 120Hz to roughly 1 second,
which moves into being intolerable. FasterSwiper allows me to shorten the animation
duration and customize the animation easing curve; my personal preference is a
quadratic ease out over 200ms.

## Building

On Apple Silicon with full Xcode installed, build and test locally without
installing, signing the app bundle or launching it:

```bash
./tools/build-command.sh
```

The script bootstraps a checksum-pinned Bazel locally, uses a project-local
cache, and sets `DEVELOPER_DIR` without changing `xcode-select`. It exports
`artifacts/FasterSwiper.app` and `artifacts/FasterSwiper-source.zip`.
Apple Silicon's linker emits an
ad-hoc signature; there is no Developer ID signing or notarization. See
[FORK.md](FORK.md) for exact commands and remaining manual acceptance checks.

## Contributing

### Linting and Formatting

FasterSwiper uses [Aspect CLI](https://aspect.build/docs/cli). Install Aspect
CLI with Homebrew:

```bash
brew install aspect-build/aspect/aspect
```

- To run a format pass on your local changes:
  ```bash
  aspect format
  ```
  Or on the entire repo:
  ```bash
  aspect format --scope=all
  ```

- To run a lint pass on your local changes:
  ```bash
  aspect lint
  ```
  To try to apply fixes automatically:
  ```bash
  aspect lint --fix
  ```

### Language Server (compile_commands.json)

If you use VS Code, you may find it useful to generate a `compile_commands.json`
file so the C++ language server can properly index the codebase:

```bash
bazel run @hedron_compile_commands//:refresh_all
```

Then restart VS Code.
