// One notification, one process. It expires by itself.
//
//   wweft notify.wren <slot> <summary> [body] [urgency]
//
// A D-Bus daemon owns org.freedesktop.Notifications, hands out the slot
// number, and spawns one of these for each notification. Nothing keeps a
// queue: each process holds its own timeout and its own place on screen,
// and a crash takes down one notification instead of all of them.

import "wweft" for Surface, Grid, Style, Sys

var ARGS = Sys.args
var SLOT = ARGS.count > 0 ? (Num.fromString(ARGS[0]) || 0) : 0
var SUMMARY = ARGS.count > 1 ? ARGS[1] : "Notification"
var BODY = ARGS.count > 2 ? ARGS[2] : ""
var URGENCY = ARGS.count > 3 ? ARGS[3] : "normal"

var COLS = 40
var ROWS = 2
var HEIGHT = ROWS + 2          // the border adds one cell on each side

// Palette. Critical is louder and stays much longer.
var CRITICAL = URGENCY == "critical"
var FG = 0xffe6e6e6
var BG = 0xf2181a1d
var ACCENT = CRITICAL ? 0xffe06c5a : 0xff7fbfff
var TITLE = Style.define(ACCENT, BG)
var BODY_ST = Style.define(0xff9aa2ab, BG)
var FRAME = Style.define(ACCENT, BG)

class Notification {
  construct new() {}

  onDraw(g) {
    g.fill(0, 0, g.cols, g.rows, BODY_ST)
    g.text(0, 0, clip(SUMMARY, g.cols), TITLE)
    if (BODY != "") g.text(0, 1, clip(BODY, g.cols), BODY_ST)
  }

  clip(text, width) {
    if (Grid.width(text) <= width) return text
    return text[0...(width - 1)] + "…"
  }
}

Surface.font("", 16)
Surface.layer("overlay")
Surface.anchor("top-right")
Surface.margin((SLOT * (HEIGHT + 1) + 1) * Surface.cellH, Surface.cellW, 0, 0)
// A notification must never take the keyboard: it would steal focus, and the
// one before it would dismiss itself.
Surface.keyboard(false)
Surface.exclusive(0)      // sit under a bar, do not reserve anything
Surface.border("round", FRAME)
Surface.window(COLS, ROWS)
Surface.lifetime(CRITICAL ? 15000 : 5000)
Surface.run(Notification.new())
