#!/bin/bash
# Build and test the fork without launching it or signing the app bundle.
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

if [[ "$(uname -s)/$(uname -m)" != "Darwin/arm64" ]]; then
    printf '%s\n' 'This local bootstrap is pinned for Apple Silicon macOS only.' >&2
    exit 1
fi
if [[ "$(<.bazelversion)" != "9.2.0" ]]; then
    printf '%s\n' 'Update the reviewed Bazel version and SHA256 together.' >&2
    exit 1
fi
xcrun --find swiftc >/dev/null
mkdir -p .local/tools artifacts
bazel="$PWD/.local/tools/bazel"
expected=dd466352a3e4d3581b8898740ee1ff208866ccbe25f8d367c5dcb950219587e6
if [[ ! -f "$bazel" ]]; then
    curl --fail --location --retry 2 \
        https://github.com/bazelbuild/bazel/releases/download/9.2.0/bazel-9.2.0-darwin-arm64 \
        --output "$bazel.download"
    actual=$(shasum -a 256 "$bazel.download")
    [[ "${actual%% *}" == "$expected" ]] || { printf '%s\n' 'Bazel checksum mismatch' >&2; exit 1; }
    mv "$bazel.download" "$bazel"
fi
actual=$(shasum -a 256 "$bazel")
[[ "${actual%% *}" == "$expected" ]] || { printf '%s\n' 'Bazel checksum mismatch' >&2; exit 1; }
chmod +x "$bazel"

common=(-c opt --repo_contents_cache= --jobs="${BAZEL_JOBS:-6}")
# --build_tests_only avoids compiling the unrelated developer tools under src/tools.
"$bazel" --output_user_root="$PWD/.local/bazel" test "${common[@]}" \
    --build_tests_only --test_output=errors //src/...
python3 tools/export-test-results.py
"$bazel" --output_user_root="$PWD/.local/bazel" build "${common[@]}" \
    //src/app:FasterSwiper_app

# Export only to this repository's ignored build-artifact directory.
# Never touch /Applications or another project's working app.
archive=bazel-bin/src/app/FasterSwiper_app.zip
stage=$(mktemp -d "$PWD/.local/export.XXXXXXXX")
trap 'rm -rf "$stage"' EXIT
/usr/bin/ditto -x -k "$archive" "$stage"
app='FasterSwiper.app'
[[ -x "$stage/$app/Contents/MacOS/FasterSwiper" ]]
rm -rf "artifacts/$app"
/usr/bin/ditto "$stage/$app" "artifacts/$app"
# Bazel outputs are read-only; -f replaces an earlier exported ZIP safely.
cp -f "$archive" artifacts/FasterSwiper-source.zip
shasum -a 256 artifacts/FasterSwiper-source.zip \
    "artifacts/$app/Contents/MacOS/FasterSwiper" > artifacts/SHA256SUMS
printf '\nBuilt, tested, not launched: %s/artifacts/%s\n' "$PWD" "$app"
