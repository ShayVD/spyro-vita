// A fake Vita3K for v3k_keys_test.sh: a window that logs every key event it gets (as Vita3K's SDL frontend would
// see it) to $FAKE_LOG, "ready" once its window is up, and the stop line at the release of Escape.
//   v3k_keys_fake --frontend sdl [-c CONFIG] -w VPK
import AppKit

let logPath = ProcessInfo.processInfo.environment["FAKE_LOG"] ?? "/dev/null"
FileManager.default.createFile(atPath: logPath, contents: nil)
let logFile = FileHandle(forWritingAtPath: logPath)!
func log(_ s: String) { logFile.seekToEndOfFile(); logFile.write((s + "\n").data(using: .utf8)!) }

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let win = NSWindow(contentRect: NSRect(x: 80, y: 80, width: 320, height: 180), styleMask: [.titled],
                   backing: .buffered, defer: false)
win.title = "v3k_keys_fake"
win.makeKeyAndOrderFront(nil)
NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { e in
  switch e.type {
  case .keyDown: if !e.isARepeat { log("key down \(e.keyCode)") }
  case .keyUp:
    log("key up \(e.keyCode)")
    if e.keyCode == 53 { log("frames=1 reached"); exit(0) }
  default: log("flags \(e.keyCode) shift=\(e.modifierFlags.contains(.shift) ? 1 : 0)")
  }
  return nil
}
DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { log("ready") }
log("boot")
app.run()
