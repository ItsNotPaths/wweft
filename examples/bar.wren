// A bar. Workspaces on the left, a clock in the middle, nothing else.
//
// It never takes the keyboard: a surface that reserves space asks for none
// unless Surface.keyboard says otherwise. Everything it knows arrives on the
// Hyprland event socket, and the clock comes from the timer.
//
//   exec-once = wweft ~/.config/wweft/bar.wren

import "wweft" for Surface, Grid, Style, Sys
import "hypr" for Hypr

// Palette. Four numbers, 0xAARRGGBB.
var FG = 0xffcfd4da
var BG = 0xff15181c
var ACCENT = 0xff7fbfff
var BASE = Style.define(FG, BG)
var CLOCK = Style.define(FG, BG)
var TAG = Style.define(0xff6b7078, BG)
var TAG_ON = Style.define(BG, ACCENT)

class Bar {
  // Wren reads % as the start of an interpolation, so a strftime format
  // needs a backslash on each one.
  construct new() {
    _ws = Hypr.workspaces
    _active = Hypr.active
    _clock = Sys.strftime("\%a \%d \%b   \%H:\%M")
  }

  onTick() {
    _clock = Sys.strftime("\%a \%d \%b   \%H:\%M")
  }

  onMessage(line) {
    var e = Hypr.event(line)

    if (e[0] == "workspace") {
      _active = Num.fromString(e[1]) || _active
    } else if (e[0] == "createworkspace" || e[0] == "destroyworkspace") {
      _ws = Hypr.workspaces
      _active = Hypr.active
    }
  }

  onDraw(g) {
    g.fill(0, 0, g.cols, g.rows, BASE)

    var x = 1
    for (id in _ws) {
      g.text(x, 0, " %(id) ", id == _active ? TAG_ON : TAG)
      x = x + 3
    }

    g.center(0, _clock, CLOCK)
  }
}

Surface.font("", 16)
Surface.layer("top")
Surface.anchor("top")
Surface.exclusive(1)      // reserve one row. No keyboard, by default
Surface.every(1000)       // onTick, on the second boundary
Surface.listen(Hypr.socket)
Surface.window(0, 1)
Surface.run(Bar.new())
