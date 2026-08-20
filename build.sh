#!/bin/sh
# Build wweft. Run download-deps.sh one time before this script.
#   ./build.sh          release build with cc
#   ./build.sh debug    no optimization, symbols, warnings as errors
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$root/build"
mode=${1:-release}
CC=${CC:-cc}

mkdir -p "$out"

if [ ! -f "$root/vendor/proto/wlr-layer-shell-unstable-v1-protocol.c" ]; then
	echo "dependencies are missing. run ./download-deps.sh" >&2
	exit 1
fi

src=$(find "$root/src" -name '*.c' | sort)
if [ -z "$src" ]; then
	echo "no source files in src/" >&2
	exit 1
fi

inc="-I$root/src -I$root/vendor/proto -I$root/vendor/stb -I$root/vendor/font"
inc="$inc -I$root/vendor/wren/include -I$root/vendor/wren/vm -I$root/vendor/wren/optional"

feat="-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700"
warn="-Wall -Wextra -Wno-unused-parameter"
case "$mode" in
	debug)
		cflags="-std=c11 -O0 -g3 -DDEBUG $feat $warn -Werror"
		ldflags=""
		;;
	release)
		cflags="-std=c11 -Os -flto -ffunction-sections -fdata-sections $feat $warn"
		ldflags="-flto -Wl,--gc-sections"
		;;
	*)
		echo "unknown mode: $mode" >&2
		exit 1
		;;
esac

libs=$(pkg-config --libs wayland-client xkbcommon)

proto=$(find "$root/vendor/proto" -name '*.c' | sort)
wren=""
[ -d "$root/vendor/wren/vm" ] && wren=$(find "$root/vendor/wren/vm" "$root/vendor/wren/optional" -name '*.c' | sort)

# Wren is third party code. It builds without the warning flags.
objs=""
compile() {
	f=$1; extra=$2
	o="$out/$(echo "${f#$root/}" | tr '/' '_' | sed 's/\.c$/.o/')"
	$CC $cflags $extra $inc -c "$f" -o "$o"
	objs="$objs $o"
}

for f in $src $proto; do compile "$f" ""; done
for f in $wren; do compile "$f" "-w"; done

$CC $ldflags $objs $libs -lm -o "$out/wweft"

if [ "$mode" != debug ]; then
	strip "$out/wweft"
fi

printf '==> %s  %s bytes\n' "$out/wweft" "$(stat -c%s "$out/wweft")"
