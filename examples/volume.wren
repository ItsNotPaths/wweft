// Volume popup.
//
// It talks to PipeWire through wpctl, from the wireplumber package. This is
// the sound stack on Omarchy and on Arch. There is no D-Bus call and no
// systemd unit: wpctl speaks to the PipeWire socket directly, so this is
// the shortest path.
//
//   Left, Right   volume down and up, 5 percent each step
//   Down, Up      the same
//   m             mute and unmute
//   Escape        close
//
// Bind it:  bind = , XF86AudioRaiseVolume, exec, wweft ~/.config/wweft/volume.wren

import "wweft" for Surface, Grid, Style, Key

var SINK = "@DEFAULT_AUDIO_SINK@"
var STEP = "5\%"          // Wren needs the backslash for a literal percent
var WIDTH = 30          // cells in the bar

// Palette. Four numbers, 0xAARRGGBB. Change them and the popup changes.
var FG = 0xffd8e0d0
var BG = 0xf2161a16
var ACCENT = 0xff8ec07c
var TITLE = Style.define(ACCENT, BG)
var ITEM = Style.define(FG, BG)
var DIM = Style.define(0xff6a706a, BG)

// A bar is a filled rectangle, not a row of block characters. A block
// character is only as wide as the font draws it, so it leaves gaps.
var BAR_ON = Style.define(ACCENT, ACCENT)
var BAR_OFF = Style.define(0xff2c322c, 0xff2c322c)
var BAR_MUTED = Style.define(0xff5a605a, 0xff5a605a)

class Volume {
  construct new() {
    _level = 0          // 0.0 to 1.0
    _muted = false
    read()
  }

  // "Volume: 0.40" or "Volume: 0.40 [MUTED]"
  read() {
    var out = Surface.sh("wpctl get-volume %(SINK)")[0].trim()
    _muted = out.contains("MUTED")

    var parts = out.split(" ")
    if (parts.count > 1) {
      _level = Num.fromString(parts[1])
      if (_level == null) _level = 0
    }
  }

  change(direction) {
    Surface.sh("wpctl set-volume %(SINK) %(STEP)%(direction)")
    read()
  }

  toggleMute() {
    Surface.sh("wpctl set-mute %(SINK) toggle")
    read()
  }

  onKey(k) {
    if (k == Key.right || k == Key.up) {
      change("+")
    } else if (k == Key.left || k == Key.down) {
      change("-")
    } else if (k == "m") {
      toggleMute()
    } else if (k == Key.escape || k == Key.enter) {
      Surface.close(0)
    } else {
      return false
    }
    return true
  }

  onDraw(g) {
    var percent = (_level * 100).round
    var filled = (_level * WIDTH).round.min(WIDTH)
    var style = _muted ? DIM : ITEM

    g.fill(0, 0, g.cols, g.rows, ITEM)   // the palette background
    g.text(2, 1, _muted ? "Muted" : "Volume", TITLE)
    g.text(g.cols - 6, 1, "%(percent)\%", style)

    g.fill(2, 3, filled, 1, _muted ? BAR_MUTED : BAR_ON)
    g.fill(2 + filled, 3, WIDTH - filled, 1, BAR_OFF)
  }
}

Surface.font("", 16)
Surface.layer("overlay")
Surface.anchor("bottom")
Surface.margin(0, 0, 4, 0)
Surface.window(WIDTH + 4, 5)
Surface.run(Volume.new())
