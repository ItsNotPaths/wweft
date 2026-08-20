import "wweft" for Surface, Grid, Style, Key

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
    g.text(2, 1, "Session", Style.title)
    var x = 2
    for (i in 0..._items.count) {
      g.text(x, 3, " %(_items[i]) ", i == _sel ? Style.sel : Style.item)
      x = x + g.width(_items[i]) + 3
    }
  }
}

Surface.font("", 16)
Surface.layer("overlay")
Surface.anchor("center")
Surface.window(46, 5)
Surface.run(Session.new())
