// A box that runs, jumps, and wobbles when it lands.
//
// Nothing here draws a scene. The box IS the surface: wweft moves it with
// Surface.margin, in the compositor's own pixels, and squashes it with
// Surface.window, in cells. The screen edges come from Surface.screenW and
// screenH. Arrow keys or hjkl, space to jump, Escape to leave.
//
// The box is held by its bottom middle, not its top left, and it is anchored
// to the edge it is squashing against. A resize and a move do not land in the
// same frame, so a top left anchor lets the box hang a cell above the floor
// through every wobble. An anchored edge cannot move: the box grows away
// from it instead.

import "wweft" for Surface, Grid, Style, Key

var CELLS_W = 18            // the box at rest, in cells
var CELLS_H = 9
var GRAVITY = 1.5
var THRUST = 1.7            // while a run key is still counting down
var THRUST_TICKS = 9        // one press is worth this many ticks of push
var DRAG = 0.988
var GRIP = 0.90             // the floor, once nothing is pushing
var TOP_SPEED = 24
var JUMP = -27
var BOUNCE = 0.62           // how much speed an edge gives back
var PLANT = 4               // ticks held against the edge, compressing

class Box {
  construct new() {
    _cx = Surface.screenW / 2      // the middle of the box
    _by = Surface.screenH / 3      // the bottom of the box
    _vx = 0
    _vy = 0
    _squash = 1             // over 1 is wide and short, under 1 is tall
    _wobble = 0
    _run = 0                // ticks of push still owed to a key press
    _way = 0
    _side = 0               // the wall it last hit: 1 right, -1 left
    _plantY = 0             // ticks still stuck to the floor or ceiling
    _plantX = 0             // ticks still stuck to a wall
    _kickY = 0              // the speed waiting for the box to let go
    _kickX = 0
    _face = 0
    _skin = Style.define(0xff102030, 0xfff2b134)
    _eye = Style.define(0xfff2b134, 0xff102030)
  }

  // The two numbers the surface is asked for. The area stays about the
  // same, so the box reads as one thing made of one material.
  cols { (CELLS_W * _squash).round.max(6) }
  rows { (CELLS_H / _squash).round.max(4) }

  w { cols * Surface.cellW }
  h { rows * Surface.cellH }

  onFloor { _plantY > 0 || _by >= Surface.screenH - 1 }

  // An edge is hit in the same tick the squash has to show. Setting the
  // wobble alone puts the squash one tick late, and by then the rebound has
  // already carried the box off the edge: fast landings would then flatten
  // out in mid air, a long way above the floor.
  hit(amount) {
    _squash = (1 + amount).max(0.55).min(1.8)
    _wobble = 0
    _face = 10
  }

  // A spring that pulls the squash back to 1 and overshoots on the way.
  settle() {
    _wobble = _wobble + (1 - _squash) * 0.30
    _wobble = _wobble * 0.80
    _squash = (_squash + _wobble).max(0.55).min(1.8)
    if (_face > 0) _face = _face - 1
  }

  step() {
    // The squash comes first. The edges below are found with the size the
    // box is about to have, not the size it just had.
    settle()

    var half = w / 2
    var sw = Surface.screenW
    var sh = Surface.screenH

    across(half, sw)
    down(sh)

    // The squash is set by a hit, so the width can change after the walls
    // were checked. A landing near a wall would push the wider box off the
    // side of the screen.
    half = w / 2
    _cx = _cx.max(half).min(sw - half)
    _by = _by.max(h).min(sh)

    Surface.window(cols, rows)
    place(half, sw, sh)
  }

  across(half, sw) {
    if (_run > 0) {
      _vx = (_vx + _way * THRUST).max(-TOP_SPEED).min(TOP_SPEED)
      _run = _run - 1
    } else if (onFloor) {
      _vx = _vx * GRIP
    }

    // Stuck to a wall: it holds still and compresses, then lets go.
    if (_plantX > 0) {
      _plantX = _plantX - 1
      _cx = _side == 1 ? sw - half : half
      if (_plantX == 0) _vx = _kickX
      return
    }

    _vx = _vx * DRAG
    _cx = _cx + _vx

    // Only a box moving into the wall has hit it. A squash that widens the
    // box can push it past the edge by itself, and that is a clamp, not a
    // hit: counting it re-squashes every frame and the shape flickers.
    if (_cx - half < 0 && _vx < 0) {
      _cx = half
      _side = -1
    } else if (_cx + half > sw && _vx > 0) {
      _cx = sw - half
      _side = 1
    } else {
      return
    }

    if (_vx.abs > 4) {
      hit(-(_vx.abs / 90).min(0.42))
      _kickX = -_vx * BOUNCE
      _plantX = PLANT
      _vx = 0
    } else {
      _vx = -_vx * BOUNCE
    }
  }

  down(sh) {
    // Stuck to the floor: planted while the squash does its work, so the
    // flat pose is seen against the floor and not above it.
    if (_plantY > 0) {
      _plantY = _plantY - 1
      if (_plantY == 0) _vy = _kickY
      return
    }

    _vy = _vy + GRAVITY
    _by = _by + _vy

    if (_by > sh && _vy > 0) {
      _by = sh
      if (_vy > 4) {
        hit((_vy / 70).min(0.55))
        _kickY = -_vy * BOUNCE
        _plantY = PLANT
        _vy = 0
      } else {
        _vy = 0
      }
    } else if (_by - h < 0 && _vy < 0) {
      _by = h
      if (_vy.abs > 4) hit((_vy.abs / 90).min(0.4))
      _vy = -_vy * BOUNCE
    }
  }

  // The bottom is always anchored. The side is whichever wall was hit last,
  // so a squash there grows inward instead of pulling the box off the wall.
  place(half, sw, sh) {
    if (_side == 1) {
      Surface.anchor("bottom-right")
      Surface.margin(0, (sw - _cx - half).round, (sh - _by).round, 0)
    } else {
      Surface.anchor("bottom-left")
      Surface.margin(0, 0, (sh - _by).round, (_cx - half).round)
    }
  }

  // One press buys a few ticks of push. A key repeat only ever follows the
  // last key pressed, so a jump would otherwise cancel a held run key and
  // stop the box dead in the air.
  push(way) {
    _way = way
    _run = THRUST_TICKS
  }

  jump() {
    if (!onFloor) return
    _plantY = 0
    _vy = JUMP
    hit(-0.30)
  }

  draw() {
    var r = Grid.rows

    Grid.fill(0, 0, Grid.cols, r, _skin)
    Grid.center((r / 3).floor, _face > 0 ? "^  ^" : "o  o", _eye)
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
Surface.window(CELLS_W, CELLS_H)
Surface.dismiss(false)
Surface.every(16)
Surface.run(Game.new())
