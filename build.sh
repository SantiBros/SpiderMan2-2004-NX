#!/usr/bin/env bash
# build.sh: builds the Switch port from YOUR copy of Spider-Man 2 (Xbox).
#
#   bash build.sh [--yes] [--copy-game] <your game>
#
# <your game> is any of: your disc image (.iso .xiso .cci .cso, whole or the first part of a split one), the
# folder with the disc's files (default.xbe and data/), or default.xbe itself. Only default.xbe is read for the
# build: the game's data stays where it is, and goes on your SD card next to the port.
#
#   --copy-game   also copy your game into the SD card folder it makes, so that folder is all there is to copy
#   --yes         install missing tools without asking (Ubuntu, Debian, WSL)
#
# It checks the tools (and offers to install them on Ubuntu or Debian), checks that your default.xbe is the release
# the port supports, recompiles it to C++, builds SpiderMan2Xbox.nro with devkitA64, and lays out the SD card folder
# in out/sd/. On Windows, build-windows.cmd does all of it through WSL.
# The .nro holds code made from your copy of the game: it is for you only, never share or upload it.
#
# Needs (see docs/BUILDING.md): Linux or Windows with WSL; git, cmake 3.20+, ninja, a C++20 compiler (g++ 11+ or
# clang 14+), and devkitPro's devkitA64, libnx and switch-tools (the switch-dev group) and switch-mesa (OpenGL for
# the GPU renderer) under $DEVKITPRO.
set -euo pipefail

R=$(cd "$(dirname "$0")" && pwd)
cd "$R"
VERSION=$(tr -d ' \r\n' < VERSION)
SUPPORTED_SHA1=853bcb253c5baea2d7e2d9b59e5888b4c87bb402  # Spider-Man 2, title 4156002B, version 4
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }

yes_all=0 copy_game=0 src=
while [ $# -gt 0 ]; do
    case $1 in
        --yes | -y) yes_all=1 ;;
        --copy-game) copy_game=1 ;;
        -h | --help) sed -n "2,/^set -euo/{/^#/s/^# \{0,1\}//p}" "$0"; exit 0 ;;
        *) [ -z "$src" ] || fail "one game at a time: $src and $1"; src=$1 ;;
    esac
    shift
done

echo "Spider-Man 2 (Xbox) for Switch $VERSION: build from your own copy of the game"

# 1. tools
say "[1/5] Checking the tools"
# what is missing, one line each; empty when everything is there
missing_tools() {
    for t in git cmake sha1sum wget; do command -v $t > /dev/null || echo "$t"; done
    command -v ninja > /dev/null || echo "ninja (ninja-build)"
    command -v c++ > /dev/null || command -v g++ > /dev/null || command -v clang++ > /dev/null || echo "a C++ compiler (g++)"
    [ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] && [ -f "$DEVKITPRO/libnx/switch.specs" ] &&
        [ -x "$DEVKITPRO/tools/bin/elf2nro" ] || echo "devkitPro's Switch tools (switch-dev)"
    [ -f "$DEVKITPRO/portlibs/switch/lib/libEGL.a" ] && [ -f "$DEVKITPRO/portlibs/switch/lib/libdrm_nouveau.a" ] ||
        echo "OpenGL for the Switch (switch-mesa)"
}
# everything from Ubuntu's or Debian's packages and devkitPro's: once, about 10 minutes
install_tools() {
    say "Installing the tools (once, about 10 minutes). sudo asks for your password: on WSL, the one of your Linux user"
    sudo apt-get update || fail "Ubuntu's package list could not be updated (the lines above say why)"
    sudo apt-get install -y git cmake ninja-build g++ wget curl ca-certificates ||
        fail "Ubuntu's packages could not be installed (the lines above say why)"
    if ! command -v dkp-pacman > /dev/null; then
        [ -e /etc/mtab ] || sudo ln -s /proc/self/mounts /etc/mtab  # WSL may lack it, devkitPro's installer needs it
        local tmp url=https://apt.devkitpro.org/install-devkitpro-pacman
        tmp=$(mktemp -d)
        echo "  Downloading devkitPro's installer: $url"
        fetch "$url" "$tmp/install-devkitpro-pacman" || fail "devkitPro's installer could not be downloaded from
  $url
  The lines above say why. Check that Linux is online (run: wget -O /dev/null $url), then start the
  builder again. Some networks, VPNs and antivirus programs block that site: try another network if it keeps
  failing."
        chmod +x "$tmp/install-devkitpro-pacman"
        sudo "$tmp/install-devkitpro-pacman" || fail "devkitPro's installer stopped (the lines above say why)"
        rm -rf "$tmp"
        command -v dkp-pacman > /dev/null || fail "devkitPro's installer finished but dkp-pacman is not there"
    fi
    sudo dkp-pacman -Sy --noconfirm || fail "devkitPro's package list could not be downloaded (the lines above say why)"
    sudo dkp-pacman -S --needed --noconfirm switch-dev switch-mesa ||
        fail "devkitPro's Switch tools could not be installed (the lines above say why)"
    export DEVKITPRO=/opt/devkitpro
}
# url into file, its errors shown: wget, then curl, three times, and only a script counts (not a web page)
fetch() {
    local i
    for i in 1 2 3; do
        { wget -O "$2" "$1" || { command -v curl > /dev/null && curl -fL -o "$2" "$1"; }; } &&
            [ "$(head -c 2 "$2" 2> /dev/null)" = "#!" ] && return 0
        [ $i = 3 ] || { echo "  Download failed (try $i of 3), trying again in 5 seconds"; sleep 5; }
    done
    return 1
}
missing=$(missing_tools)
if [ -n "$missing" ]; then
    echo "  Missing:"
    echo "$missing" | sed 's/^/    /'
    if command -v apt-get > /dev/null; then
        answer=y
        if [ $yes_all = 0 ]; then
            printf '  Install them now? [Y/n] '
            { read -r answer < /dev/tty; } 2> /dev/null || answer=n
        fi
        case ${answer:-y} in
            [Yy]* | "") install_tools ;;
            *) fail "the tools are needed: see docs/INSTALL.md#building-the-port" ;;
        esac
        missing=$(missing_tools)
        [ -z "$missing" ] || fail "still missing after installing: $(echo $missing)
  See docs/INSTALL.md#building-the-port to install them by hand."
    else
        fail "install them with your distribution's packages (git cmake ninja g++ wget), then devkitPro
  (https://devkitpro.org/wiki/Getting_Started) and: sudo dkp-pacman -S switch-dev switch-mesa
  Open a new terminal afterwards (it sets DEVKITPRO), or run: export DEVKITPRO=/opt/devkitpro"
    fi
fi
echo "  devkitA64: $("$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" -dumpversion), $JOBS jobs"

# the PC tools: xbe_recomp, xbe_extract
cmake -S tools/xbe -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release > build-tools.log 2>&1 &&
    cmake --build build-tools >> build-tools.log 2>&1 || fail "the PC tools did not build: see build-tools.log"

# 2. your default.xbe
say "[2/5] Reading default.xbe from your copy"
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

# your game beside it: a disc image with all its parts, or default.xbe and the data folder
if [ $copy_game = 1 ]; then
    echo "  Copying your game into $sd/game/"
    if [ -d "$src" ] && [ -n "$(find "$src" -maxdepth 1 -iname default.xbe | head -1)" ]; then
        cp "$(find "$src" -maxdepth 1 -iname default.xbe | head -1)" "$sd/game/default.xbe"
        data=$(find "$src" -maxdepth 1 -type d -iname data | head -1)
        [ -n "$data" ] && cp -r "$data" "$sd/game/data" || echo "  (no data folder beside default.xbe: copy it yourself)"
    elif [[ "${src,,}" == *.xbe ]]; then
        cp "$src" "$sd/game/default.xbe"
        data=$(find "$(dirname "$src")" -maxdepth 1 -type d -iname data | head -1)
        [ -n "$data" ] && cp -r "$data" "$sd/game/data" || echo "  (no data folder beside default.xbe: copy it yourself)"
    else
        image=$src
        [ -d "$src" ] && image=$(find "$src" -maxdepth 1 -type f \( -iname '*.iso' -o -iname '*.xiso' -o -iname '*.cci' -o -iname '*.cso' \) | sort | head -1)
        name=$(basename "$image")
        if [[ "$name" =~ ^(.*)\.1\.([A-Za-z]+)$ ]]; then
            # a split image: every part
            for part in "$(dirname "$image")/${BASH_REMATCH[1]}".*."${BASH_REMATCH[2]}"; do cp "$part" "$sd/game/"; done
        else
            cp "$image" "$sd/game/"
        fi
    fi
fi

if [ $copy_game = 1 ]; then
    step2="  2. Your game is already in it, in  switch/SpiderMan2Xbox/game/."
else
    step2="  2. Put your game in  switch/SpiderMan2Xbox/game/  on the card: your disc image (all its parts if it is
     split), or default.xbe and the data folder."
fi
cat <<EOF

Done. Now:
  1. Copy the folder  out/sd/switch  to the root of your SD card (merge it with the switch folder there).
$step2
  3. Hold R while you start any game, then start "Spider-Man 2 (Xbox)" in the Homebrew Menu.

The .nro holds code made from your copy of the game: keep it for yourself, do not share or upload it.
EOF
