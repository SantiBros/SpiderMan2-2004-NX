#!/usr/bin/env bash
# build.sh: builds the Switch port from YOUR copy of Spider-Man 2 (Xbox).
#
#   bash build.sh <your game>
#
# <your game> is any of: your disc image (.iso .xiso .cci .cso, whole or the first part of a split one), the
# folder with the disc's files (default.xbe and data/), or default.xbe itself. Only default.xbe is read: the
# game's data stays where it is, and goes on your SD card next to the port.
#
# It checks the tools, checks that your default.xbe is the release the port supports, recompiles it to C++,
# builds SpiderMan2Xbox.nro with devkitA64, and lays out the SD card folder in out/sd/.
# The .nro holds code made from your copy of the game: it is for you only, never share or upload it.
#
# Needs (see docs/BUILDING.md): Linux or Windows with WSL; git, cmake 3.20+, ninja, a C++20 compiler (g++ 11+ or
# clang 14+), and devkitPro's devkitA64, libnx and switch-tools (the switch-dev group) under $DEVKITPRO.
set -euo pipefail

R=$(cd "$(dirname "$0")" && pwd)
cd "$R"
VERSION=$(tr -d ' \r\n' < VERSION)
SUPPORTED_SHA1=853bcb253c5baea2d7e2d9b59e5888b4c87bb402  # Spider-Man 2, title 4156002B, version 4
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }

echo "Spider-Man 2 (Xbox) for Switch $VERSION: build from your own copy of the game"

# 1. tools
say "[1/5] Checking the tools"
missing=()
for t in git cmake sha1sum; do command -v $t > /dev/null || missing+=("$t"); done
command -v ninja > /dev/null || missing+=("ninja (ninja-build)")
command -v c++ > /dev/null || command -v g++ > /dev/null || command -v clang++ > /dev/null || missing+=("a C++ compiler (g++)")
[ ${#missing[@]} -eq 0 ] || fail "missing: ${missing[*]}
  On Ubuntu or Debian (also in WSL): sudo apt install git cmake ninja-build g++"
[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] && [ -f "$DEVKITPRO/libnx/switch.specs" ] &&
    [ -x "$DEVKITPRO/tools/bin/elf2nro" ] ||
    fail "devkitPro's Switch tools were not found under $DEVKITPRO.
  Install devkitPro (https://devkitpro.org/wiki/Getting_Started), then: sudo dkp-pacman -S switch-dev
  Open a new terminal afterwards (it sets DEVKITPRO), or run: export DEVKITPRO=/opt/devkitpro"
echo "  devkitA64: $("$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" -dumpversion), $JOBS jobs"

# the PC tools: xbe_recomp, xbe_extract
cmake -S tools/xbe -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release > build-tools.log 2>&1 &&
    cmake --build build-tools >> build-tools.log 2>&1 || fail "the PC tools did not build: see build-tools.log"

# 2. your default.xbe
say "[2/5] Reading default.xbe from your copy"
src=${1:-}
if [ -z "$src" ] && [ -f assets/default.xbe ]; then
    src=assets/default.xbe
fi
[ -n "$src" ] || fail "say where your game is: bash build.sh <disc image, game folder or default.xbe>"
[ -e "$src" ] || fail "$src does not exist"
mkdir -p assets
xbe=$(mktemp)
trap 'rm -f "$xbe"' EXIT
if [ -d "$src" ]; then
    found=$(find "$src" -maxdepth 1 -iname default.xbe | head -1)
    if [ -n "$found" ]; then
        cp "$found" "$xbe"
    else
        # a folder with a disc image in it
        image=$(find "$src" -maxdepth 1 -type f \( -iname '*.iso' -o -iname '*.xiso' -o -iname '*.cci' -o -iname '*.cso' \) | sort | head -1)
        [ -n "$image" ] || fail "$src has neither default.xbe nor a disc image (.iso .xiso .cci .cso)"
        build-tools/xbe_extract "$image" "$xbe" || fail "default.xbe could not be read from $image"
    fi
else
    case "${src,,}" in
        *.xbe) cp "$src" "$xbe" ;;
        *.zip | *.7z | *.rar) fail "$src is a compressed archive: extract the disc image or the game's folder from it first" ;;
        *) build-tools/xbe_extract "$src" "$xbe" || fail "default.xbe could not be read from $src" ;;
    esac
fi
sha=$(sha1sum "$xbe" | cut -c1-40)
echo "  default.xbe SHA-1 $sha"
[ "$sha" = "$SUPPORTED_SHA1" ] || fail "this default.xbe is not the release the port supports.
  Supported: Spider-Man 2 for the original Xbox, title 4156002B version 4, default.xbe SHA-1 $SUPPORTED_SHA1
  Yours:     $sha
  The recompiled code is made for that one file; other releases need their own config (see docs/TECHNICAL.md)."
cp "$xbe" assets/default.xbe

# 3. recompile
say "[3/5] Recompiling default.xbe to C++ (a few seconds)"
build-tools/xbe_recomp config/game.toml --xdk sdk/xdk > build-recomp.log 2>&1 || fail "recompiling failed: see build-recomp.log"
grep -m1 "^total:" build-recomp.log | sed 's/^total: /  /' || true

# 4. the .nro
say "[4/5] Building SpiderMan2Xbox.nro ($JOBS jobs: about $(( 20 / JOBS + 2 )) minutes)"
JOBS=$JOBS VERSION=$VERSION bash switch/build.sh > build-switch.log 2>&1 || fail "the Switch build failed: see build-switch.log"
[ -f build-switch/SpiderMan2Xbox.nro ] || fail "no .nro was made: see build-switch.log"

# 5. the SD card folder
say "[5/5] Laying out the SD card folder"
sd=out/sd/switch/SpiderMan2Xbox
rm -rf out/sd
mkdir -p "$sd/game"
cp build-switch/SpiderMan2Xbox.nro "$sd/"
cp switch/README-switch.txt "$sd/README.txt"
cp switch/game-folder-README.txt "$sd/game/PUT_YOUR_GAME_HERE.txt"
echo "  $(du -h "$sd/SpiderMan2Xbox.nro" | cut -f1)  $sd/SpiderMan2Xbox.nro"

cat <<EOF

Done. Now:
  1. Copy the folder  out/sd/switch  to the root of your SD card (merge it with the switch folder there).
  2. Put your game in  switch/SpiderMan2Xbox/game/  on the card: your disc image (all its parts if it is
     split), or default.xbe and the data folder.
  3. Hold R while you start any game, then start "Spider-Man 2 (Xbox)" in the Homebrew Menu.

The .nro holds code made from your copy of the game: keep it for yourself, do not share or upload it.
EOF
