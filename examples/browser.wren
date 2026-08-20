// A file navigator. It looks and moves, and it changes nothing.
//
//   Up, Down       move
//   Right, Return  enter a directory. On a file, print the path and exit
//   Left           go to the parent directory
//   Escape         give up, exit code 1
//
// It starts in the working directory. Nothing here deletes, renames, or
// writes: the only shell command is `ls`.

import "wweft" for Surface, Grid, Style, Key, Text

var ROWS = 16
var COLS = 64

// Palette. Four numbers, 0xAARRGGBB. Change them and the browser changes.
var FG = 0xffe4dcc8
var BG = 0xf21b1a16
var ACCENT = 0xffe0b050
var DIR = Style.define(ACCENT, BG)
var FILE = Style.define(FG, BG)
var SEL = Style.define(BG, ACCENT)
var DIM = Style.define(0xff6e6a5e, BG)

class Browser {
  construct new() {
    _dir = Surface.sh("pwd")[0].trim()
    load()
  }

  load() {
    // -p marks a directory with a trailing slash, -A keeps dotfiles.
    _entries = [".."]
    var cmd = "ls -1Ap --group-directories-first " + Text.quote(_dir)
    for (name in Surface.lines(cmd)) {
      _entries.add(name)
    }
    _sel = 0
    _top = 0
  }

  parent(path) {
    var cut = -1
    for (i in 0...path.count) {
      if (path[i] == "/") cut = i
    }
    if (cut <= 0) return "/"
    return path[0...cut]
  }

  join(name) {
    if (_dir == "/") return "/" + name
    return _dir + "/" + name
  }

  move(step) {
    _sel = (_sel + step).max(0).min(_entries.count - 1)
    if (_sel < _top) _top = _sel
    if (_sel >= _top + ROWS) _top = _sel - ROWS + 1
  }

  open() {
    var name = _entries[_sel]

    if (name == "..") {
      _dir = parent(_dir)
      load()
    } else if (name.endsWith("/")) {
      _dir = join(name[0...-1])
      load()
    } else {
      Surface.emit(join(name))
      Surface.close(0)
    }
  }

  onKey(k) {
    if (k == Key.down) {
      move(1)
    } else if (k == Key.up) {
      move(-1)
    } else if (k == Key.right || k == Key.enter) {
      open()
    } else if (k == Key.left) {
      _dir = parent(_dir)
      load()
    } else if (k == Key.escape) {
      Surface.close(1)
    } else {
      return false
    }
    return true
  }

  onDraw(g) {
    var path = _dir
    if (g.width(path) > g.cols - 10) {
      path = "..." + path[(path.count - (g.cols - 13))..-1]
    }
    g.fill(0, 0, g.cols, g.rows, FILE)   // the palette background
    g.text(1, 0, path, DIR)
    g.text(g.cols - 8, 0, "%(_sel + 1)/%(_entries.count)", DIM)

    for (row in 0...ROWS) {
      var i = _top + row
      if (i >= _entries.count) break

      var name = _entries[i]
      var isDir = name.endsWith("/") || name == ".."
      var style = i == _sel ? SEL : (isDir ? DIR : FILE)

      if (g.width(name) > g.cols - 4) name = name[0...(g.cols - 4)]
      g.fill(0, row + 1, g.cols, 1, i == _sel ? SEL : FILE)
      g.text(2, row + 1, name, style)
    }
  }
}

Surface.font("", 16)
Surface.anchor("center")
Surface.window(COLS, ROWS + 1)
Surface.run(Browser.new())
