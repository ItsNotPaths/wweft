// dmenu, in wweft.
//
// Items come from stdin, one to a line. Type to filter, arrows to move,
// Return to print the choice on stdout, Escape to give up.
//
//   ls ~/Projects | wweft ~/.config/wweft/dmenu.wren
//
// Exit code 0 with the choice on stdout, or 1 when nothing was chosen.

import "wweft" for Surface, Grid, Style, Key, Text

var ROWS = 12           // list rows on screen
var COLS = 60

// Palette. Four numbers, 0xAARRGGBB.
var FG = 0xffd8d4e0
var BG = 0xf216141c
var ACCENT = 0xffbd93f9
var PROMPT = Style.define(ACCENT, BG)
var ITEM = Style.define(FG, BG)
var SEL = Style.define(BG, ACCENT)
var DIM = Style.define(0xff6f6a80, BG)

class Menu {
  construct new() {
    _all = []
    for (line in (Surface.read("/dev/stdin") || "").split("\n")) {
      if (line.trim() != "") _all.add(line.trim())
    }
    _query = ""
    _hits = _all
    _sel = 0
    _top = 0
  }

  filter() {
    if (_query == "") {
      _hits = _all
    } else {
      _hits = []
      for (item in _all) {
        if (Text.contains(item, _query)) _hits.add(item)
      }
    }
    _sel = 0
    _top = 0
  }

  move(step) {
    if (_hits.count == 0) return
    _sel = (_sel + step).max(0).min(_hits.count - 1)
    if (_sel < _top) _top = _sel
    if (_sel >= _top + ROWS) _top = _sel - ROWS + 1
  }

  onKey(k) {
    if (k == Key.down || k == "Ctrl+n") {
      move(1)
    } else if (k == Key.up || k == "Ctrl+p") {
      move(-1)
    } else if (k == Key.backspace) {
      if (_query.count > 0) {
        _query = _query[0...-1]
        filter()
      }
    } else if (k == Key.enter) {
      if (_hits.count > 0) Surface.emit(_hits[_sel])
      Surface.close(_hits.count > 0 ? 0 : 1)
    } else if (k == Key.escape) {
      Surface.close(1)
    } else if (Key.text != "") {
      _query = _query + Key.text
      filter()
    } else {
      return false
    }
    return true
  }

  onDraw(g) {
    g.fill(0, 0, g.cols, g.rows, ITEM)
    g.text(1, 0, "> %(_query)", PROMPT)
    g.text(g.cols - 12, 0, "%(_hits.count)/%(_all.count)", DIM)

    for (row in 0...ROWS) {
      var i = _top + row
      if (i >= _hits.count) break

      var line = _hits[i]
      if (g.width(line) > g.cols - 4) line = line[0...(g.cols - 4)]
      g.fill(0, row + 1, g.cols, 1, i == _sel ? SEL : ITEM)
      g.text(2, row + 1, line, i == _sel ? SEL : ITEM)
    }
  }
}

Surface.font("", 16)
Surface.outline(6, PROMPT)      // pixels outside the cells, not over them
Surface.anchor("top")
Surface.margin(6 * Surface.cellH, 0, 0, 0)
Surface.window(COLS, ROWS + 1)
Surface.run(Menu.new())
