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

# Wren without Meta and Random. Nothing in wweft uses them, and each one is
# about 2 KB. Both builds set this, so a script behaves the same in each.
feat="$feat -DWREN_OPT_META=0 -DWREN_OPT_RANDOM=0"
warn="-Wall -Wextra -Wno-unused-parameter"
case "$mode" in
	debug)
		cflags="-std=c11 -O0 -g3 -DDEBUG $feat $warn -Werror"
		ldflags=""
		;;
	release)
		# No unwind tables: 20 KB. wweft never unwinds a stack, and the
		# debug build keeps them for the debugger.
		cflags="-std=c11 -Os -flto -ffunction-sections -fdata-sections $feat $warn"
		cflags="$cflags -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident"
		ldflags="-flto -Wl,--gc-sections -Wl,--build-id=none"
		;;
	*)
		echo "unknown mode: $mode" >&2
		exit 1
		;;
esac

cflags="$cflags ${EXTRA_CFLAGS:-}"
ldflags="$ldflags ${EXTRA_LDFLAGS:-}"
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
	[ -n "${NOSTRIP:-}" ] || strip --strip-all -R .comment "$out/wweft"
fi

printf '==> %s  %s bytes\n' "$out/wweft" "$(stat -c%s "$out/wweft")"
