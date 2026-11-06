#!/usr/bin/env python3
"""Verify the exported app's identity and decoded icon, without launching it.

Uses Python's standard library, iconutil, and sips. Writes only to artifacts/.
Run after tools/build-command.sh, from any working directory.
"""
import hashlib
import json
import plistlib
import struct
import subprocess
import zlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "artifacts/FasterSwiper.app"
EVIDENCE = ROOT / "artifacts/icon-packaging"
EVIDENCE.mkdir(parents=True, exist_ok=True)


def png_dimensions(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    width, height, depth, color = struct.unpack(">IIBB", data[16:26])
    compressed = bytearray()
    offset = 8
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        assert zlib.crc32(kind + payload) & 0xFFFFFFFF == crc, path
        if kind == b"IDAT":
            compressed.extend(payload)
        offset += 12 + length
    assert compressed and zlib.decompress(compressed), path
    return {"width": width, "height": height, "depth": depth, "color_type": color}


def run(*args):
    result = subprocess.run(args, check=True, text=True, capture_output=True)
    return result.stdout


info = plistlib.loads((APP / "Contents/Info.plist").read_bytes())
assert info["CFBundleIdentifier"] == "com.martin.FasterSwiper.Command"
assert info["CFBundleName"] == info["CFBundleDisplayName"] == "FasterSwiper"
assert info["CFBundleIconName"] == "AppIcon"
assert info["CFBundleIconFile"] in ("AppIcon", "AppIcon.icns")
assert info["LSMinimumSystemVersion"] == "26.0"
with zipfile.ZipFile(ROOT / "artifacts/FasterSwiper-source.zip") as archive:
    for packaged in APP.rglob("*"):
        if packaged.is_file():
            relative = packaged.relative_to(APP.parent).as_posix()
            assert archive.read(relative) == packaged.read_bytes(), relative
resources = APP / "Contents/Resources"
for name in ("LICENSE", "ATTRIBUTION.md"):
    assert (resources / name).read_bytes() == (ROOT / name).read_bytes()
icon = resources / "AppIcon.icns"
raw = icon.read_bytes()
assert raw[:4] == b"icns" and struct.unpack(">I", raw[4:8])[0] == len(raw)
assert (resources / "Assets.car").stat().st_size > 0
blocks = []
offset = 8
while offset < len(raw):
    kind, length = struct.unpack(">4sI", raw[offset:offset + 8])
    assert length >= 8 and offset + length <= len(raw)
    blocks.append({"type": kind.decode("ascii"), "length": length})
    offset += length
assert offset == len(raw)

# iconutil performs a real decode; sips validates each decoded bitmap as an image.
decoded = EVIDENCE / "AppIcon.iconset"
if decoded.exists():
    import shutil
    shutil.rmtree(decoded)
run("/usr/bin/iconutil", "--convert", "iconset", "--output", str(decoded), str(icon))
images = {}
for image in sorted(decoded.glob("*.png")):
    dimensions = png_dimensions(image)
    output = run("/usr/bin/sips", "--getProperty", "pixelWidth", "--getProperty", "pixelHeight", str(image))
    assert f"pixelWidth: {dimensions['width']}" in output
    assert f"pixelHeight: {dimensions['height']}" in output
    images[image.name] = dimensions
# Xcode's native .icon pipeline deliberately emits a small compatibility ICNS
# (16/32/128/256 px here); the full 16..1024 multi-size set is in Assets.car.
assert {16, 32, 128, 256}.issubset({d["width"] for d in images.values()})
asset_info = json.loads(run("/usr/bin/assetutil", "--info", str(resources / "Assets.car")))
(EVIDENCE / "asset-catalog-info.json").write_text(json.dumps(asset_info, indent=2) + "\n")
multisize = [a for a in asset_info if a.get("AssetType") == "MultiSized Image" and a.get("Name") == "AppIcon"]
assert len(multisize) == 1
catalog_sizes = {int(size.split("x")[0]) for size in multisize[0]["Sizes"]}
assert {16, 32, 64, 128, 256, 512, 1024}.issubset(catalog_sizes)
renditions = [a for a in asset_info if a.get("AssetType") == "Icon Image" and a.get("Name") == "AppIcon"]
assert {32, 64, 128, 256, 512, 1024}.issubset({a["PixelWidth"] for a in renditions})
assert any(a.get("Name") == "AppIcon" and a.get("AssetType") == "IconImageStack"
           and a.get("CanvasWidth") == a.get("CanvasHeight") == 1024 for a in asset_info)

# The committed PNG catalog is retained as editable/reference-size artwork;
# deployment >=26 uses the native Icon Composer bundle, not this legacy catalog.
catalog = ROOT / "src/app/Assets.xcassets/AppIcon.appiconset"
manifest = json.loads((catalog / "Contents.json").read_text())
assert len(manifest["images"]) == 10
source_images = {}
for entry in manifest["images"]:
    expected = int(entry["size"].split("x")[0]) * int(entry["scale"][0])
    dimensions = png_dimensions(catalog / entry["filename"])
    assert dimensions["width"] == dimensions["height"] == expected
    source_images[entry["filename"]] = dimensions

files = [APP / "Contents/MacOS/FasterSwiper", APP / "Contents/Info.plist", icon,
         resources / "Assets.car", ROOT / "artifacts/FasterSwiper-source.zip"]
report = {
    "bundle_identifier": info["CFBundleIdentifier"],
    "bundle_name": info["CFBundleName"],
    "icon_file": info["CFBundleIconFile"],
    "icon_name": info["CFBundleIconName"],
    "icns_blocks": blocks,
    "decoded_images": images,
    "asset_catalog_icon_sizes": sorted(catalog_sizes),
    "source_catalog_images": source_images,
    "sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    "app_launched": False,
}
(EVIDENCE / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
