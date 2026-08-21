#!/bin/sh
# Build wweft. Run download-deps.sh one time before this script.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$root/build"
CC=${CC:-cc}

usage() {
	cat <<EOF
usage: $(basename "$0") [debug] [--public --version vX.Y.Z [--notes "text"] [--prerelease]]

  (no flag)             release build into build/, which git ignores
  debug                 no optimization, symbols, warnings as errors
  --public              trigger release.yml through the gh CLI
  --version <tag>       required with --public. It also stamps a local build
  --notes <text>        release notes
  --prerelease          mark the release as a pre-release
EOF
}

mode=release
do_public=0
version=""
notes=""
prerelease=false

while [ $# -gt 0 ]; do
	case "$1" in
		debug|release) mode="$1"; shift ;;
		--public)      do_public=1; shift ;;
		--version)     version="${2:-}"; shift 2 ;;
		--notes)       notes="${2:-}"; shift 2 ;;
		--prerelease)  prerelease=true; shift ;;
		-h|--help)     usage; exit 0 ;;
		*) echo "unknown flag: $1" >&2; usage; exit 1 ;;
	esac
done

# ----------------------------------------------------------------- public
# It builds nothing here. The workflow builds from the commit that GitHub
# has, so a release never carries a local change that was never pushed.
if [ "$do_public" = 1 ]; then
	workflow=release.yml

	if [ -z "$version" ]; then
		echo "error: --public needs --version <tag>" >&2
		exit 1
	fi
	if ! command -v gh >/dev/null 2>&1; then
		echo "error: no gh CLI. Install it and run 'gh auth login'" >&2
		exit 1
	fi

	repo=$(gh repo view --json nameWithOwner -q '.nameWithOwner' 2>/dev/null || true)
	if [ -z "$repo" ]; then
		echo "error: not a GitHub repository, or gh is not authenticated" >&2
		exit 1
	fi

	printf '==> trigger %s on %s (%s)\n' "$workflow" "$repo" "$version"
	old_id=$(gh run list --workflow="$workflow" --limit 1 \
		--json databaseId -q '.[0].databaseId' 2>/dev/null || echo "")

	gh workflow run "$workflow" \
		--field version="$version" \
		--field notes="$notes" \
		--field prerelease="$prerelease"

	echo "==> waiting for the run to appear"
	new_id=""
	i=0
	while [ "$i" -lt 30 ]; do
		i=$((i + 1))
		sleep 2
		cur_id=$(gh run list --workflow="$workflow" --limit 1 \
			--json databaseId -q '.[0].databaseId' 2>/dev/null || echo "")
		if [ -n "$cur_id" ] && [ "$cur_id" != "$old_id" ]; then
			new_id="$cur_id"
			break
		fi
	done

	if [ -z "$new_id" ]; then
		echo "error: no new workflow run appeared" >&2
		exit 1
	fi

	printf '==> watching run %s\n' "$new_id"
	gh run watch "$new_id" --exit-status
	exit 0
fi

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

# The licence texts go inside the binary, so that one file carries its own
# notices. `wweft --license` prints them.
"$root/tools/txt2h.sh" wweft_notices \
	"$root/LICENSE" "$root/THIRD-PARTY.md" > "$out/notices.h"

# The Wren module. Its comments are for the reader of src/wweft.wren, not
# for the binary, so they are dropped on the way in.
sed '/^[[:space:]]*\/\//d' "$root/src/wweft.wren" > "$out/module.wren"
"$root/tools/txt2h.sh" wweft_module "$out/module.wren" > "$out/module.h"

inc="-I$root/src -I$root/vendor/proto -I$root/vendor/stb -I$root/vendor/font -I$out"
inc="$inc -I$root/vendor/wren/include -I$root/vendor/wren/vm -I$root/vendor/wren/optional"

feat="-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700"

# Wren without Meta and Random. Nothing in wweft uses them, and each one is
# about 2 KB. Both builds set this, so a script behaves the same in each.
feat="$feat -DWREN_OPT_META=0 -DWREN_OPT_RANDOM=0"

# A local build is not a tagged one, so it says so instead of claiming a
# release number. The workflow passes --version.
feat="$feat -DWWEFT_VERSION=\"${version:-dev}\""
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
esac

cflags="$cflags ${EXTRA_CFLAGS:-}"
ldflags="$ldflags ${EXTRA_LDFLAGS:-}"
libs=$(pkg-config --libs wayland-client xkbcommon)

proto=$(find "$root/vendor/proto" -name '*.c' | sort)
# optional/ holds Meta and Random, and both are off. They would compile to
# nothing, so they are not compiled at all.
wren=""
[ -d "$root/vendor/wren/vm" ] && wren=$(find "$root/vendor/wren/vm" -name '*.c' | sort)

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
