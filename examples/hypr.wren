// Hyprland state for a bar. This is the only compositor specific file: a
// workspace means something different on every compositor, so it lives in
// Wren and not in C.
//
// Put it next to the script that imports it, or in ~/.config/wweft/.

import "wweft" for Surface, Sys

class Hypr {
  // The event stream. It pushes a line every time something changes, so
  // nothing has to poll.
  static socket {
    var run = Sys.env("XDG_RUNTIME_DIR")
    var sig = Sys.env("HYPRLAND_INSTANCE_SIGNATURE")
    return "%(run)/hypr/%(sig)/.socket2.sock"
  }

  // "workspace ID 2 (2) on monitor eDP-1:" -> 2
  static idOf(line) {
    var parts = line.split(" ")
    if (parts.count < 3) return null
    return Num.fromString(parts[2])
  }

  // One fork, at start. After this the socket keeps the list current.
  static workspaces {
    var out = []
    for (line in Surface.lines("hyprctl workspaces")) {
      var id = line.startsWith("workspace ID ") ? idOf(line) : null
      if (id != null) out.add(id)
    }
    return sorted(out)
  }

  static active {
    for (line in Surface.lines("hyprctl activeworkspace")) {
      if (line.startsWith("workspace ID ")) return idOf(line)
    }
    return 1
  }

  // "workspace>>3" -> ["workspace", "3"]
  static event(line) {
    var at = line.indexOf(">>")
    if (at < 0) return [line, ""]
    return [line[0...at], line[(at + 2)..-1]]
  }

  static sorted(list) {
    for (i in 1...list.count) {
      var v = list[i]
      var j = i - 1
      while (j >= 0 && list[j] > v) {
        list[j + 1] = list[j]
        j = j - 1
      }
      list[j + 1] = v
    }
    return list
  }
}
