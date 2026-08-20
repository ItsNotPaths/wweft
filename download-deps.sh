#!/bin/sh
# Get all dependencies of wweft. Run this script one time after you clone.
# It installs the system packages, copies the protocol files, and downloads
# the vendored sources.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
vendor="$root/vendor"

WREN_VERSION=0.4.0
SPLEEN_VERSION=2.1.0
WLR_PROTOCOLS_COMMIT=a741f0a
STB_COMMIT=2c980bb59875b0d32144a71867fbdebb2f77cd20

force=0
[ "${1:-}" = "--force" ] && force=1

say() { printf '==> %s\n' "$1"; }

# ---------------------------------------------------------------- packages
# Arch is the machine we develop on. apt is what the CI runner has. Anything
# else installs the three libraries by hand.
if [ "$(id -u)" = 0 ]; then SUDO=""; else SUDO="sudo"; fi

if command -v pacman >/dev/null 2>&1; then
	pkgs="wayland wayland-protocols libxkbcommon wlr-protocols"
	missing=""
	for p in $pkgs; do
		pacman -Q "$p" >/dev/null 2>&1 || missing="$missing $p"
	done
	if [ -n "$missing" ]; then
		say "pacman install:$missing"
		$SUDO pacman -S --needed --noconfirm $missing
	else
		say "packages are installed"
	fi
elif command -v apt-get >/dev/null 2>&1; then
	say "apt install"
	$SUDO apt-get update -qq
	$SUDO apt-get install -y --no-install-recommends \
		libwayland-dev libwayland-bin wayland-protocols \
		libxkbcommon-dev pkg-config python3
else
	say "unknown package manager"
	echo "install wayland, wayland-protocols, and libxkbcommon yourself" >&2
fi

# --------------------------------------------------------------- protocols
# The generated code goes in the tree. The build needs no scanner.
proto_dir="$vendor/proto"
mkdir -p "$proto_dir"

gen() {
	name=$1
	xml=$2
	if [ ! -f "$xml" ]; then
		echo "missing protocol file: $xml" >&2
		exit 1
	fi
	cp -f "$xml" "$proto_dir/$name.xml"
	wayland-scanner client-header "$xml" "$proto_dir/$name-client-protocol.h"
	wayland-scanner private-code  "$xml" "$proto_dir/$name-protocol.c"
	say "protocol $name"
}

wp=$(pkg-config --variable=pkgdatadir wayland-protocols)
layer_xml=/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml

if [ "$force" = 1 ] || [ ! -f "$proto_dir/wlr-layer-shell-unstable-v1-protocol.c" ]; then
	# Only Arch packages wlr-protocols. Everywhere else, take the one file
	# we need from the repository, at a pinned commit.
	if [ ! -f "$layer_xml" ]; then
		say "download wlr-layer-shell $WLR_PROTOCOLS_COMMIT"
		layer_xml="$proto_dir/.wlr-layer-shell-unstable-v1.xml"
		curl -fsSL -o "$layer_xml" \
			"https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/$WLR_PROTOCOLS_COMMIT/unstable/wlr-layer-shell-unstable-v1.xml"
	fi
	gen wlr-layer-shell-unstable-v1 "$layer_xml"
	gen xdg-shell "$wp/stable/xdg-shell/xdg-shell.xml"
else
	say "protocols are ready"
fi

# -------------------------------------------------------------------- wren
if [ "$force" = 1 ] || [ ! -f "$vendor/wren/include/wren.h" ]; then
	say "wren $WREN_VERSION"
	rm -rf "$vendor/wren" "$vendor/.wren-tmp"
	mkdir -p "$vendor/.wren-tmp" "$vendor/wren"
	curl -fsSL "https://github.com/wren-lang/wren/archive/refs/tags/$WREN_VERSION.tar.gz" \
		| tar -xzf - -C "$vendor/.wren-tmp" --strip-components=1
	cp -r "$vendor/.wren-tmp/src/include"  "$vendor/wren/include"
	cp -r "$vendor/.wren-tmp/src/vm"       "$vendor/wren/vm"
	cp -r "$vendor/.wren-tmp/src/optional" "$vendor/wren/optional"
	cp    "$vendor/.wren-tmp/LICENSE"      "$vendor/wren/LICENSE"
	rm -rf "$vendor/.wren-tmp"
else
	say "wren is ready"
fi

# --------------------------------------------------------------------- stb
if [ "$force" = 1 ] || [ ! -f "$vendor/stb/stb_truetype.h" ]; then
	say "stb_truetype $STB_COMMIT"
	mkdir -p "$vendor/stb"
	curl -fsSL -o "$vendor/stb/stb_truetype.h" \
		"https://raw.githubusercontent.com/nothings/stb/$STB_COMMIT/stb_truetype.h"
else
	say "stb_truetype is ready"
fi

# ---------------------------------------------------------- fallback font
# The 8x16 bitmap font of D9. The header goes in the tree. The BDF does not.
if [ "$force" = 1 ] || [ ! -f "$vendor/font/spleen8x16.h" ]; then
	say "spleen $SPLEEN_VERSION"
	mkdir -p "$vendor/font" "$vendor/.spleen-tmp"
	curl -fsSL "https://github.com/fcambus/spleen/releases/download/$SPLEEN_VERSION/spleen-$SPLEEN_VERSION.tar.gz" \
		| tar -xzf - -C "$vendor/.spleen-tmp" --strip-components=1
	python3 "$root/tools/bdf2h.py" "$vendor/.spleen-tmp/spleen-8x16.bdf" \
		"$vendor/font/spleen8x16.h" spleen8x16 32 126
	cp "$vendor/.spleen-tmp/LICENSE" "$vendor/font/LICENSE.spleen"
	rm -rf "$vendor/.spleen-tmp"
else
	say "fallback font is ready"
fi

say "done"
