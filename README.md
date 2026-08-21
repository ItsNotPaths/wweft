# wweft

WM agnostic wren TUI's/OSD's with no tty spawn/baggage.

<img src="imgs/reel.gif" width="720" alt="a box bouncing off every screen edge, then becoming a menu that gets typed into">
<img src="imgs/apps.png" width="720" alt="a session menu, a volume popup, a dmenu, a file browser, and a bar">

wweft puts a grid of text cells on a `wlr-layer-shell` surface and gives a
Wren script the content. One binary, ~170 KB, no toolkit. A menu, a bar,
a volume popup, and a file browser are wren scripts.

## Build

```sh
./download-deps.sh     # system packages, Wren, stb_truetype, the fallback font
./build.sh             # build/wweft
```

Arch and Debian are handled. Anything else needs `wayland`,
`wayland-protocols`, and `libxkbcommon` installed first.

## Run

```sh
wweft examples/session.wren
ls ~/Projects | wweft examples/dmenu.wren
```

```ini
# hypr/bindings.conf
bind = SUPER, ESCAPE, exec, wweft ~/.config/wweft/session.wren
```

The choice goes to stdout. Exit code 0 means chosen, 1 means cancelled.

## API

Tagged in source, in reading order. The C file holds what the surface and
the grid do, `src/wweft.wren` holds the helpers written in Wren:

```sh
cat src/script_wren.c src/wweft.wren | grep -o '@api.*'
```

Key names come from xkbcommon: `a`, `A`, `Left`, `Ctrl+Left`, `Escape`.

The surface is placed in the compositor's own pixels. Glyph space is a
multiplication away:

```wren
Surface.font("", 20)                            // one number sets the cell
Surface.margin(3 * Surface.cellH, 0, 0, 0)      // three rows down
Surface.window(40, 6)                           // the window is glyph space
```

Every `Surface` call also works after the first frame. Change the size, the
position, the font or the border from `onKey` or `onTick`, and wweft applies
the lot in one commit before the next frame. A font change keeps the window
at the same number of cells, so the surface grows and the layout does not
move.

`import "theme" for Theme` searches the script directory, then
`$XDG_CONFIG_HOME/wweft`, then `~/.config/wweft`. Config is Wren.

## Examples

| File | What |
|---|---|
| `examples/session.wren` | power menu, `spawn` runs the choice |
| `examples/volume.wren` | PipeWire volume through `wpctl` |
| `examples/dmenu.wren` | items on stdin, type to filter |
| `examples/browser.wren` | file navigator, read only |
| `examples/bar.wren` | a bar: workspaces, a clock, no keyboard |
| `examples/hypr.wren` | Hyprland state for `bar.wren`, imported by name |
| `examples/notify.wren` | one notification, one process, ends itself |
| `examples/bounce.wren` | a box that runs, jumps and wobbles. The surface is the box |

The palette is four numbers at the top of each script.

## Environment

| Name | Effect |
|---|---|
| `WWEFT_FONT` | path of a TTF file |
| `WWEFT_SIZE` | cell height in pixels, default 16 |
| `WWEFT_DEBUG` | key names to stderr |
| `WWEFT_DUMP` | write each frame to a file, as a PAM image |
| `WWEFT_NO_FRACTIONAL` | fall back to whole number scaling |

## License

MIT. It carries Wren, stb_truetype, and the Spleen font.
`wweft --license` prints every notice.
