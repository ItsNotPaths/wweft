// A box that runs, jumps, and wobbles when it lands.
//
// Nothing here draws a scene. The box IS the surface: wweft moves it with
// Surface.margin, in the compositor's own pixels, and squashes it with
// Surface.window, in cells. The screen edges come from Surface.screenW and
// screenH. Arrow keys or hjkl, space to jump, Escape to leave.

import "wweft" for Surface, Grid, Style, Key

var CELLS_W = 18            // the box at rest, in cells
var CELLS_H = 9
var GRAVITY = 1.5
var SHOVE = 4.5             // one key press
var DRAG = 0.88
var TOP_SPEED = 26
var JUMP = -27
var BOUNCE = 0.62           // how much speed a wall gives back
var FLOOR_GRIP = 0.80

class Box {
  construct new() {
    _x = Surface.screenW / 2 - CELLS_W * Surface.cellW / 2
    _y = Surface.screenH / 3
    _vx = 0
    _vy = 0
    _squash = 1             // over 1 is wide and short, under 1 is tall
    _wobble = 0
    _face = 0
    _skin = Style.define(0xff102030, 0xfff2b134)
    _eye = Style.define(0xfff2b134, 0xff102030)
  }

  // The two the surface is asked for. Area stays about the same, so the
  // box looks like it is made of one thing.
  cols { (CELLS_W * _squash).round.max(6) }
  rows { (CELLS_H / _squash).round.max(4) }

  w { cols * Surface.cellW }
  h { rows * Surface.cellH }

  // A spring pulling the squash back to 1, never quite settling at once.
  wobbleOn(amount) {
    _wobble = _wobble + amount
    _face = 6
  }

  step() {
    var right = Surface.screenW - w
    var floor = Surface.screenH - h

    _vy = _vy + GRAVITY
    _vx = _vx * DRAG
    _x = _x + _vx
    _y = _y + _vy

    if (_x < 0) {
      _x = 0
      _vx = -_vx * BOUNCE
      wobbleOn(-(_vx.abs / 90).min(0.42))
    } else if (_x > right) {
      _x = right
      _vx = -_vx * BOUNCE
      wobbleOn(-(_vx.abs / 90).min(0.42))
    }

    if (_y > floor) {
      _y = floor
      if (_vy > 3) wobbleOn((_vy / 70).min(0.55))
      _vy = -_vy * BOUNCE
      _vx = _vx * FLOOR_GRIP
      if (_vy.abs < 3) _vy = 0
    } else if (_y < 0) {
      _y = 0
      _vy = -_vy * BOUNCE
      wobbleOn((_vy.abs / 90).min(0.4))
    }

    _wobble = _wobble + (0 - _squash + 1) * 0.30
    _wobble = _wobble * 0.80
    _squash = (_squash + _wobble).max(0.55).min(1.8)
    if (_face > 0) _face = _face - 1

    Surface.window(cols, rows)
    Surface.margin(_y.round, 0, 0, _x.round)
  }

  push(way) {
    _vx = (_vx + way * SHOVE).max(-TOP_SPEED).min(TOP_SPEED)
  }

  jump() {
    if (_y >= Surface.screenH - h - 2) {
      _vy = JUMP
      wobbleOn(-0.30)
    }
  }

  draw() {
    var c = Grid.cols
    var r = Grid.rows
    var eyes = _face > 0 ? "^  ^" : "o  o"

    Grid.fill(0, 0, c, r, _skin)
    Grid.center((r / 3).floor, eyes, _eye)
    Grid.center((r * 2 / 3).floor, _face > 0 ? "----" : "\\__/", _eye)
  }
}

class Game {
  construct new() { _box = Box.new() }

  onDraw(g) { _box.draw() }
  onTick() { _box.step() }

  onKey(name) {
    if (name == Key.escape || name == "q") Surface.close(0)
    if (name == Key.left || name == "h") _box.push(-1)
    if (name == Key.right || name == "l") _box.push(1)
    if (name == Key.space || name == Key.up || name == "k") _box.jump()
    return true
  }
}

Surface.font("", 16)
Surface.layer("overlay")
Surface.anchor("top-left")   // margin is measured from here, so it is x and y
Surface.window(CELLS_W, CELLS_H)
Surface.dismiss(false)
Surface.every(16)
Surface.run(Game.new())
