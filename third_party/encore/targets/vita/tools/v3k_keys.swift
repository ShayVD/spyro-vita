// Plays a key script into one Vita3K process as macOS keyboard events (CGEvent.postToPid), which Vita3K's keyboard
// map turns into the Vita's buttons and sticks: the real controller path, which a pad script (PAD=) bypasses.
// Used by v3k_run.sh KEYS=FILE (macOS only):
//   v3k_keys --check SCRIPT [CONFIG_YML]          parse the script, check the names and that events may be posted
//   v3k_keys PID SCRIPT LOG [CONFIG_YML]          play it into PID (v3k_run.sh's own Vita3K), LOG the game's log
// The script, one step a line ('#' starts a comment):
//   wait S              sleep S seconds (a decimal)
//   log RE [TIMEOUT]    wait until the game's log matches the regular expression RE (ICU, as grep -E for the usual
//                       cases; no spaces: use \s), up to TIMEOUT s (default 600)
//   press NAMES [HOLD]  NAMES down in order, held HOLD s (default 0.2), then up in reverse order
//   down NAMES / up NAMES
// NAMES joins names with '+' (select+l1). A name is a Vita input, mapped to a key by Vita3K's config.yml
// (keyboard-button-cross: KeyX, keyboard-leftstick-up: KeyW; CONFIG_YML, else Vita3K's defaults): cross circle
// square triangle start select up down left right l1 r1 l2 r2 l3 r3 psbutton, lstick-up lstick-down lstick-left
// lstick-right, rstick-up rstick-down rstick-left rstick-right; or a key itself, key:KeyX (Vita3K's key names).
// Each step is printed with a timestamp to stdout. Vita3K is brought to the front once, before the first key.
// Exit: 0 played; 1 a bad script, a name with no key or events may not be posted (System Settings, Privacy &
// Security, Accessibility: the app the run is started from); 2 the process ended first; 3 a log step timed out.
import AppKit
import CoreGraphics
import Foundation

func fail(_ code: Int32, _ why: String) -> Never {
  FileHandle.standardError.write(("v3k_keys: " + why + "\n").data(using: .utf8)!)
  exit(code)
}

// macOS virtual key codes of Vita3K's key names (SDL scancode names as its config.yml writes them)
let keyCodes: [String: CGKeyCode] = [
  "KeyA": 0, "KeyS": 1, "KeyD": 2, "KeyF": 3, "KeyH": 4, "KeyG": 5, "KeyZ": 6, "KeyX": 7, "KeyC": 8, "KeyV": 9,
  "KeyB": 11, "KeyQ": 12, "KeyW": 13, "KeyE": 14, "KeyR": 15, "KeyY": 16, "KeyT": 17, "KeyO": 31, "KeyU": 32,
  "KeyI": 34, "KeyP": 35, "KeyL": 37, "KeyJ": 38, "KeyK": 40, "KeyN": 45, "KeyM": 46,
  "Digit1": 18, "Digit2": 19, "Digit3": 20, "Digit4": 21, "Digit6": 22, "Digit5": 23, "Digit9": 25, "Digit7": 26,
  "Digit8": 28, "Digit0": 29, "Enter": 36, "Tab": 48, "Space": 49, "Backspace": 51, "Escape": 53,
  "ShiftLeft": 56, "ShiftRight": 60, "ArrowLeft": 123, "ArrowRight": 124, "ArrowDown": 125, "ArrowUp": 126,
  "F11": 103,
]
// Vita3K's default keyboard map (config.yml keyboard-*), by the script's names
var inputs: [String: String] = [
  "select": "ShiftRight", "start": "Enter", "up": "ArrowUp", "right": "ArrowRight", "down": "ArrowDown",
  "left": "ArrowLeft", "l1": "KeyQ", "r1": "KeyE", "l2": "KeyU", "r2": "KeyO", "l3": "KeyF", "r3": "KeyH",
  "triangle": "KeyV", "circle": "KeyC", "cross": "KeyX", "square": "KeyZ", "psbutton": "KeyP",
  "lstick-left": "KeyA", "lstick-right": "KeyD", "lstick-up": "KeyW", "lstick-down": "KeyS",
  "rstick-left": "KeyJ", "rstick-right": "KeyL", "rstick-up": "KeyI", "rstick-down": "KeyK",
]
func readMap(_ path: String) {
  guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { fail(1, "cannot read \(path)") }
  for line in text.split(separator: "\n") {
    let kv = line.split(separator: ":", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
    guard kv.count == 2, kv[0].hasPrefix("keyboard-") else { continue }
    var name = String(kv[0].dropFirst("keyboard-".count))
    if name.hasPrefix("button-") { name = String(name.dropFirst("button-".count)) }
    else if name.hasPrefix("leftstick-") { name = "lstick-" + name.dropFirst("leftstick-".count) }
    else if name.hasPrefix("rightstick-") { name = "rstick-" + name.dropFirst("rightstick-".count) }
    else { continue }
    if inputs[name] != nil { inputs[name] = kv[1] }
  }
}

enum Step {
  case wait(Double), log(NSRegularExpression, String, Double), keys(Bool?, [(String, CGKeyCode)], Double)
}
func codes(_ names: Substring, _ n: Int) -> [(String, CGKeyCode)] {
  return names.split(separator: "+").map { s -> (String, CGKeyCode) in
    let name = String(s)
    let key = name.hasPrefix("key:") ? String(name.dropFirst(4)) : inputs[name]
    guard let key else { fail(1, "line \(n): \(name): not a Vita input (or key:NAME)") }
    guard let c = keyCodes[key] else { fail(1, "line \(n): \(name) is key \(key), which v3k_keys can't post") }
    return (name, c)
  }
}
func seconds(_ s: Substring?, _ dflt: Double, _ n: Int) -> Double {
  guard let s else { return dflt }
  guard let v = Double(s), v >= 0 else { fail(1, "line \(n): \(s): not a number of seconds") }
  return v
}
func parse(_ path: String) -> [Step] {
  guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { fail(1, "cannot read \(path)") }
  var steps: [Step] = []
  for (i, raw) in text.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
    let n = i + 1
    let w = raw.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first!
      .split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == "\r" })
    guard let op = w.first else { continue }
    switch (op, w.count) {
    case ("wait", 2): steps.append(.wait(seconds(w[1], 0, n)))
    case ("log", 2), ("log", 3):
      guard let re = try? NSRegularExpression(pattern: String(w[1])) else { fail(1, "line \(n): a bad regex") }
      steps.append(.log(re, String(w[1]), seconds(w.count > 2 ? w[2] : nil, 600, n)))
    case ("press", 2), ("press", 3): steps.append(.keys(nil, codes(w[1], n), seconds(w.count > 2 ? w[2] : nil, 0.2, n)))
    case ("down", 2): steps.append(.keys(true, codes(w[1], n), 0))
    case ("up", 2): steps.append(.keys(false, codes(w[1], n), 0))
    default: fail(1, "line \(n): \(raw.trimmingCharacters(in: .whitespaces)): not a step (wait, log, press, down, up)")
    }
  }
  return steps
}

let a = CommandLine.arguments
if a.count >= 3 && a[1] == "--check" {
  if a.count > 3 { readMap(a[3]) }
  let steps = parse(a[2])
  guard CGPreflightPostEventAccess() else {
    fail(1, "this process may not post keyboard events: allow the app the run is started from (Terminal, the agent) "
      + "in System Settings, Privacy & Security, Accessibility")
  }
  print("v3k_keys: \(steps.count) steps ok")
  exit(0)
}
guard a.count >= 4, let pid = pid_t(a[1]), pid > 0 else {
  fail(1, "usage: v3k_keys --check SCRIPT [CONFIG_YML] | v3k_keys PID SCRIPT LOG [CONFIG_YML]")
}
if a.count > 4 { readMap(a[4]) }
let steps = parse(a[2])
let logPath = a[3]
setvbuf(stdout, nil, _IOLBF, 0)
func say(_ s: String) { print(String(format: "%.3f ", Date().timeIntervalSince1970) + s) }
func alive() -> Bool { kill(pid, 0) == 0 || errno == EPERM }

let src = CGEventSource(stateID: .hidSystemState)
var activated = false
var flags: CGEventFlags = []
func post(_ name: String, _ code: CGKeyCode, _ down: Bool) {
  guard alive() else { say("the process is gone"); exit(2) }
  if !activated { // the window must take key events: once, before the first key
    NSRunningApplication(processIdentifier: pid)?.activate(options: [])
    Thread.sleep(forTimeInterval: 1.0)
    activated = true
  }
  let shift = code == 56 || code == 60
  if shift { // a modifier: a flagsChanged event, with the side's device bit (0x2 left, 0x4 right)
    let side = CGEventFlags(rawValue: code == 60 ? 0x4 : 0x2)
    if down { flags.insert(.maskShift); flags.insert(side) } else { flags.remove(side) }
    if !flags.contains(CGEventFlags(rawValue: 0x2)) && !flags.contains(CGEventFlags(rawValue: 0x4)) {
      flags.remove(.maskShift)
    }
  }
  guard let e = CGEvent(keyboardEventSource: src, virtualKey: code, keyDown: down) else { fail(1, "no event") }
  if shift { e.type = .flagsChanged }
  e.flags = flags
  e.postToPid(pid)
  Thread.sleep(forTimeInterval: 0.05)
}
say("v3k_keys: \(steps.count) steps into PID \(pid)")
for s in steps {
  switch s {
  case .wait(let t):
    say("wait \(t)")
    Thread.sleep(forTimeInterval: t)
  case .log(let re, let text, let limit):
    say("log \(text)")
    let t0 = Date()
    while true {
      if let log = try? String(contentsOfFile: logPath, encoding: .utf8),
         re.firstMatch(in: log, range: NSRange(log.startIndex..., in: log)) != nil { break }
      guard alive() else { say("the process is gone"); exit(2) }
      if Date().timeIntervalSince(t0) > limit { say("log \(text): not seen in \(limit) s"); exit(3) }
      Thread.sleep(forTimeInterval: 0.1)
    }
  case .keys(let dir, let ks, let hold):
    let names = ks.map { $0.0 }.joined(separator: "+")
    if dir != false {
      say("down \(names)")
      for k in ks { post(k.0, k.1, true) }
    }
    if dir == nil { Thread.sleep(forTimeInterval: hold) }
    if dir != true {
      for k in ks.reversed() { post(k.0, k.1, false) }
      say("up \(names)")
    }
  }
}
say("v3k_keys: done")
