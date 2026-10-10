// Original HSS provisional mark; no third-party artwork or downloaded assets.
// Run on macOS: swift tools/generate_app_icon.swift HSSApp/Assets.xcassets/AppIcon.appiconset/AppIcon.png
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

guard CommandLine.arguments.count == 2 else { fatalError("Supply one PNG output path") }
let size = 1024
let colorSpace = CGColorSpaceCreateDeviceRGB()
guard let context = CGContext(
    data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: size * 4,
    space: colorSpace, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue
) else { fatalError("RGB context unavailable") }
context.setFillColor(CGColor(red: 0.035, green: 0.055, blue: 0.065, alpha: 1))
context.fill(CGRect(x: 0, y: 0, width: size, height: size))
let mint = CGColor(red: 0.42, green: 0.88, blue: 0.76, alpha: 1)
context.setStrokeColor(mint)
context.setLineWidth(34)
context.setLineCap(.round)
context.setLineJoin(.round)
// Isometric room corner with an intentionally nonrectangular footprint.
// It communicates spatial geometry, not a fabricated measurement result.
let base: [CGPoint] = [
    CGPoint(x: 250, y: 342), CGPoint(x: 510, y: 192),
    CGPoint(x: 774, y: 342), CGPoint(x: 774, y: 666),
    CGPoint(x: 510, y: 824), CGPoint(x: 250, y: 666),
    CGPoint(x: 250, y: 342)
]
context.addLines(between: base)
context.strokePath()
context.addLines(between: [CGPoint(x: 250, y: 666), CGPoint(x: 510, y: 510), CGPoint(x: 774, y: 666)])
context.strokePath()
context.addLines(between: [CGPoint(x: 510, y: 510), CGPoint(x: 510, y: 824)])
context.strokePath()
// Fine dashed sightline distinguishes observation from the solid envelope.
context.setLineWidth(13)
context.setLineDash(phase: 0, lengths: [14, 25])
context.setStrokeColor(CGColor(red: 0.7, green: 0.96, blue: 0.88, alpha: 1))
context.addLines(between: [CGPoint(x: 510, y: 192), CGPoint(x: 510, y: 510)])
context.strokePath()
guard let image = context.makeImage() else { fatalError("Image unavailable") }
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
    fatalError("PNG destination unavailable")
}
CGImageDestinationAddImage(destination, image, nil)
guard CGImageDestinationFinalize(destination) else { fatalError("PNG write failed") }
