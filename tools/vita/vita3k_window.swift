// Captures Vita3K's game window (the largest window Vita3K owns) for
// tools/vita/vita3k_run.sh CAPTURE=1:
//   vita3k_window OUT.png   the window's content, title bar cut off
// Exit 0: written; 1: no Vita3K window; 2: the grab was blank (one colour:
// the window wasn't being drawn, e.g. covered or on another Space).
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
var best: CGWindowID = 0, bestArea = 0.0
for w in list {
  guard (w["kCGWindowOwnerName"] as? String ?? "").contains("Vita3K"),
        (w["kCGWindowLayer"] as? Int ?? 1) == 0,
        let num = w["kCGWindowNumber"] as? Int,
        let b = w["kCGWindowBounds"] as? [String: Double] else { continue }
  let area = (b["Width"] ?? 0) * (b["Height"] ?? 0)
  if area > bestArea { best = CGWindowID(num); bestArea = area }
}
guard best != 0,
      let shot = CGWindowListCreateImage(.null, .optionIncludingWindow, best,
                                         [.boundsIgnoreFraming, .bestResolution]) else { exit(1) }

// The title bar: the rows above the first one that is the game's (black)
// border colour all the way across, searched in the top 10%.
let w = shot.width, h = shot.height
guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                          space: CGColorSpaceCreateDeviceRGB(),
                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { exit(1) }
ctx.draw(shot, in: CGRect(x: 0, y: 0, width: w, height: h))
let px = ctx.data!.assumingMemoryBound(to: UInt8.self) // row 0 is the top
func dark(_ x: Int, _ y: Int) -> Bool {
  let p = px + (y * w + x) * 4
  return p[0] < 8 && p[1] < 8 && p[2] < 8
}
var top = 0
for y in 0..<(h / 10) where stride(from: 0, to: w, by: 7).allSatisfy({ dark($0, y) }) {
  top = y
  break
}
var lo = 255, hi = 0
for y in stride(from: top, to: h, by: 5) {
  for x in stride(from: 0, to: w, by: 5) {
    let p = px + (y * w + x) * 4
    let v = (Int(p[0]) + Int(p[1]) + Int(p[2])) / 3
    lo = min(lo, v)
    hi = max(hi, v)
  }
}
let body = shot.cropping(to: CGRect(x: 0, y: top, width: w, height: h - top))!
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
else { exit(1) }
CGImageDestinationAddImage(dest, body, nil)
CGImageDestinationFinalize(dest)
exit(hi - lo < 4 ? 2 : 0)
