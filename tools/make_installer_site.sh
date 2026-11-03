#!/usr/bin/env bash
# make_installer_site.sh: the installer page's site, from a built .nro, for GitHub Pages (the gh-pages branch), and
# the release's two built files.
#
#   bash tools/make_installer_site.sh <default.xbe> [out/site]
#
# The .nro of build-switch/ (the release's, made with switch/build.sh) is locked with default.xbe (tools/lock_nro.py)
# and put beside installer/index.html with version.json and the text files of the SD card folder. The page unlocks
# it in the player's browser with their own default.xbe. The .nro itself is never published.
# Into out/release: the locked .nro as SpiderMan2Xbox-<version>.nro.locked, and SpiderMan2Xbox-<version>-symbols.elf,
# the .elf's names and debug information without its code or data (for reading crash reports' addresses).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
XBE=${1:?usage: make_installer_site.sh <default.xbe> [out dir]}
OUT=${2:-$R/out/site}
VERSION=$(tr -d ' \r\n' < "$R/VERSION")
NRO=$R/build-switch/SpiderMan2Xbox.nro
[ -f "$NRO" ] || { echo "no $NRO: build it first (switch/build.sh)"; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
python3 "$R/tools/lock_nro.py" lock "$NRO" "$XBE" "$OUT/SpiderMan2Xbox.nro.locked"
cp "$R/installer/index.html" "$OUT/index.html"
cp "$R/switch/README-switch.txt" "$OUT/README.txt"
cp "$R/switch/game-folder-README.txt" "$OUT/PUT_YOUR_GAME_HERE.txt"
{
    cat "$R/LICENSE"
    printf '\n\n'
    cat "$R/THIRD_PARTY.md"
} > "$OUT/LICENSES.txt"
SHA1=$(python3 -c "import hashlib,sys; print(hashlib.sha1(open(sys.argv[1],'rb').read()).hexdigest())" "$XBE")
cat > "$OUT/version.json" <<EOF
{"version": "$VERSION", "locked": "SpiderMan2Xbox.nro.locked", "xbe_sha1": "$SHA1"}
EOF
touch "$OUT/.nojekyll"  # served as it is, without GitHub's Jekyll
echo "site for $VERSION in $OUT:"
ls -la "$OUT"

# the release's files
REL=$R/out/release
mkdir -p "$REL"
cp "$OUT/SpiderMan2Xbox.nro.locked" "$REL/SpiderMan2Xbox-$VERSION.nro.locked"
OBJCOPY=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-objcopy
READELF=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-readelf
SYM=$REL/SpiderMan2Xbox-$VERSION-symbols.elf
"$OBJCOPY" --only-keep-debug "$R/build-switch/SpiderMan2Xbox.elf" "$SYM"
# every section loaded into memory (code, data) must be left empty (NOBITS): only names, debug information and the
# build id (a note, to match a crash report to its build) go out
if "$READELF" -S -W "$SYM" | awk '$0 ~ /^ *\[ *[0-9]+\]/ { sub(/^ *\[ *[0-9]+\] */, ""); if ($2 != "NOBITS" && $2 != "NULL" && $2 != "NOTE" && $7 ~ /A/) { print; bad = 1 } } END { exit !bad }'; then
    echo "$SYM still holds code or data: not releasing it"
    rm -f "$SYM"
    exit 1
fi
echo "release files in $REL:"
ls -la "$REL"
