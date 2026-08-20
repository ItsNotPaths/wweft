import "wweft" for Surface, Grid, Style, Key

// Palette. Four numbers, 0xAARRGGBB. Change them and the popup changes.
var FG = 0xffe8d8c8
var BG = 0xf21d1a18
var ACCENT = 0xffe06c5a
var TITLE = Style.define(ACCENT, BG)
var ITEM = Style.define(FG, BG)
var SEL = Style.define(BG, ACCENT)

class Session {
  construct new() {
    _items = ["Suspend", "Reboot", "Power off"]
    _cmds = ["systemctl suspend", "systemctl reboot", "systemctl poweroff"]
    _sel = 0
  }

  onKey(k) {
    if (k == Key.left) {
      _sel = (_sel - 1).max(0)
    } else if (k == Key.right) {
      _sel = (_sel + 1).min(_items.count - 1)
    } else if (k == Key.enter) {
      Surface.emit(_cmds[_sel])
      Surface.close(0)
    } else if (k == Key.escape) {
      Surface.close(1)
    } else {
      return false
    }
    return true
  }

  onDraw(g) {
    g.fill(0, 0, g.cols, g.rows, ITEM)   // the palette background
    g.text(2, 1, "Session", TITLE)
    var x = 2
    for (i in 0..._items.count) {
      g.text(x, 3, " %(_items[i]) ", i == _sel ? SEL : ITEM)
      x = x + g.width(_items[i]) + 3
    }
  }
}

Surface.font("", 16)
Surface.layer("overlay")
Surface.anchor("center")
Surface.window(46, 5)
Surface.run(Session.new())
