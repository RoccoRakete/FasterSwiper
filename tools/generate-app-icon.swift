// Original FasterSwiper fork artwork. No fonts, SF Symbols, or external assets.
// From the repo root: xcrun swift tools/generate-app-icon.swift
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let output = root.appendingPathComponent("src/app/Assets.xcassets/AppIcon.appiconset")
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

func roundedRect(_ context: CGContext, _ rect: CGRect, _ radius: CGFloat, _ color: CGColor) {
  context.setFillColor(color)
  context.addPath(
    CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil))
  context.fillPath()
}
func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> CGColor {
  CGColor(srgbRed: red, green: green, blue: blue, alpha: alpha)
}

var images: [[String: String]] = []
let nativeOutput = root.appendingPathComponent("src/app/AppIcon.icon")
try FileManager.default.createDirectory(
  at: nativeOutput.appendingPathComponent("Assets"), withIntermediateDirectories: true)
let variants =
  [16, 32, 128, 256, 512].flatMap { size in [1, 2].map { (size, $0, false) } }
  + [(512, 2, true)]
for (size, scale, native) in variants {
  do {
    let pixels = size * scale
    let context = CGContext(
      data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
      bytesPerRow: pixels * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.scaleBy(x: CGFloat(pixels) / 1024, y: CGFloat(pixels) / 1024)
    // macOS icon inset; transparent corners rather than a full-bleed square.
    let tile = CGRect(x: 70, y: 70, width: 884, height: 884)
    if !native {
      context.saveGState()
      context.addPath(
        CGPath(roundedRect: tile, cornerWidth: 194, cornerHeight: 194, transform: nil))
      context.clip()
      let gradient = CGGradient(
        colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
        colors: [color(0.08, 0.29, 0.78), color(0.10, 0.62, 0.98)] as CFArray,
        locations: [0, 1])!
      context.drawLinearGradient(
        gradient, start: CGPoint(x: 512, y: 70),
        end: CGPoint(x: 512, y: 954), options: [])
      context.restoreGState()
    }
    // Overlapping desktop windows, with a clear swipe arrow underneath.
    roundedRect(context, CGRect(x: 204, y: 416, width: 474, height: 350), 42, color(1, 1, 1, 0.30))
    roundedRect(context, CGRect(x: 290, y: 344, width: 530, height: 350), 42, color(0.96, 0.99, 1))
    roundedRect(
      context, CGRect(x: 314, y: 368, width: 482, height: 254), 22, color(0.13, 0.44, 0.85))
    for x in [330, 360, 390] {
      context.setFillColor(color(0.22, 0.49, 0.77))
      context.fillEllipse(in: CGRect(x: x, y: 651, width: 14, height: 14))
    }
    context.setStrokeColor(color(1, 1, 1))
    context.setLineWidth(48)
    context.setLineCap(.round)
    context.setLineJoin(.round)
    context.move(to: CGPoint(x: 272, y: 245))
    context.addLine(to: CGPoint(x: 747, y: 245))
    context.move(to: CGPoint(x: 655, y: 337))
    context.addLine(to: CGPoint(x: 747, y: 245))
    context.addLine(to: CGPoint(x: 655, y: 153))
    context.strokePath()
    let filename = "icon_\(size)x\(size)\(scale == 2 ? "@2x" : "").png"
    let url =
      native
      ? nativeOutput.appendingPathComponent("Assets/Swipes.png")
      : output.appendingPathComponent(filename)
    let destination = CGImageDestinationCreateWithURL(
      url as CFURL,
      UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, context.makeImage()!, nil)
    precondition(CGImageDestinationFinalize(destination))
    if !native {
      images.append([
        "filename": filename, "idiom": "mac", "scale": "\(scale)x", "size": "\(size)x\(size)",
      ])
    }
  }
}
let manifest: [String: Any] = [
  "images": images, "info": ["author": "FasterSwiper fork", "version": 1],
]
let data = try JSONSerialization.data(
  withJSONObject: manifest, options: [.prettyPrinted, .sortedKeys])
try (data + Data([10])).write(to: output.appendingPathComponent("Contents.json"))
let catalogData = try JSONSerialization.data(
  withJSONObject: ["info": ["author": "FasterSwiper fork", "version": 1]],
  options: [.prettyPrinted, .sortedKeys])
try (catalogData + Data([10])).write(
  to: output.deletingLastPathComponent().appendingPathComponent("Contents.json"))
let nativeManifest: [String: Any] = [
  "fill": ["linear-gradient": ["srgb:0.08,0.29,0.78,1", "srgb:0.10,0.62,0.98,1"]],
  "groups": [
    [
      "name": "Windows and swipe",
      "layers": [
        ["name": "Swipes", "image-name": "Swipes.png", "glass": false]
      ], "shadow": ["kind": "neutral", "opacity": 0.0], "specular": false,
      "translucency": ["enabled": false, "value": 0.0],
    ]
  ],
  "supported-platforms": ["squares": ["macOS"]],
]
let nativeData = try JSONSerialization.data(
  withJSONObject: nativeManifest, options: [.prettyPrinted, .sortedKeys])
try (nativeData + Data([10])).write(to: nativeOutput.appendingPathComponent("icon.json"))
print(
  "Generated \(images.count) original app icon PNGs (16 through 1024 pixels) and macOS 26+ Icon Composer source."
)
