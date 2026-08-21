// The wweft module, imported as `import "wweft" for Surface, Grid, ...`.
// build.sh strips these comments and embeds the rest in the binary.

class Surface {
  foreign static font(path, size)
  foreign static window(cols, rows)
  foreign static anchor(spec)
  foreign static margin(t, r, b, l)
  foreign static layer(name)
  foreign static scale(n)
  foreign static exclusive(cells)
  foreign static keyboard(flag)
  foreign static dismiss(flag)
  foreign static every(ms)
  foreign static lifetime(ms)
  foreign static listen(spec)
  foreign static watch(path)
  foreign static output(name)
  foreign static borderSet(chars, style)
  foreign static outlineSet(px, style)
  foreign static close(code)
  foreign static emit(text)
  foreign static spawn(cmd)
  foreign static read(path)
  foreign static shWait(cmd, ms)
  foreign static run(object)
  foreign static cellW
  foreign static cellH
  foreign static width
  foreign static height
  foreign static scale
  foreign static screenW
  foreign static screenH
  foreign static size

  // @api Surface.sh(cmd[, ms]) -> [out, rc]  popen with a deadline
  static outline(px) { outlineSet(px, 0) }
  static outline(px, style) { outlineSet(px, style) }
  static border(chars) { borderSet(chars, 0) }
  static border(chars, style) { borderSet(chars, style) }
  static sh(cmd) { shWait(cmd, 0) }
  static sh(cmd, ms) { shWait(cmd, ms) }

  // The output of a command, one item for each line, empty lines dropped.
  // @api Surface.lines(cmd) -> list          output split, empty lines dropped
  static lines(cmd) {
    var out = []
    for (line in sh(cmd)[0].split("\n")) {
      if (line.trim() != "") out.add(line)
    }
    return out
  }
}

class Grid {
  foreign static text(x, y, str, style)
  foreign static fill(x, y, w, h, style)
  foreign static width(str)
  foreign static cols
  foreign static rows

  // @api Grid.center(y, str, style)
  static center(y, str, style) {
    text(((cols - width(str)) / 2).floor, y, str, style)
  }

  // Draw a row of items and give back the column of each one.
  // @api Grid.row(x, y, items, gap, style, selStyle, sel) -> columns
  static row(x, y, items, gap, style, selStyle, sel) {
    var at = []
    for (i in 0...items.count) {
      at.add(x)
      text(x, y, items[i], i == sel ? selStyle : style)
      x = x + width(items[i]) + gap
    }
    return at
  }
}

class Style {
  foreign static define(fg, bg)

  static setup_() {
    __base = 0
    __title = define(0xff7fbfff, 0xee151515)
    __item = define(0xffcccccc, 0xee151515)
    __sel = define(0xff151515, 0xff7fbfff)
    __dim = define(0xff707070, 0xee151515)
  }
  // @api Style.base, title, item, sel, dim   ready made style ids
  static base { __base }
  static title { __title }
  static item { __item }
  static sel { __sel }
  static dim { __dim }
}
Style.setup_()

class Sys {
  foreign static time
  foreign static strftime(format)
  foreign static env(name)
  foreign static args
}

class Text {
  // ASCII only. Wren carries no Unicode case tables.
  // @api Text.lower(s) -> str                ASCII only
  static lower(s) {
    var out = ""
    for (c in s.codePoints) {
      out = out + String.fromCodePoint(c >= 65 && c <= 90 ? c + 32 : c)
    }
    return out
  }

  // @api Text.contains(a, b) -> bool         case insensitive
  static contains(haystack, needle) {
    return lower(haystack).contains(lower(needle))
  }

  // Safe to put inside a shell command. An apostrophe in a file name
  // breaks the command without this.
  // @api Text.quote(s) -> str                safe inside a shell command
  static quote(s) {
    return "'" + s.replace("'", "'\\''") + "'"
  }
}

class Key {
  // @api Key.left, right, up, down, enter, escape, tab, space, backspace, delete, home, end
  static left { "Left" }
  static right { "Right" }
  static up { "Up" }
  static down { "Down" }
  static enter { "Return" }
  static escape { "Escape" }
  static tab { "Tab" }
  foreign static text
  static space { "space" }
  static backspace { "BackSpace" }
  static delete { "Delete" }
  static home { "Home" }
  static end { "End" }
}
