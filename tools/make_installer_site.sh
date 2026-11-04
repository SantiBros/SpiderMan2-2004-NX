#!/usr/bin/env bash
# make_installer_site.sh: the installer page's site for GitHub Pages (the gh-pages branch), with every version the
# page offers, and the release's two built files.
#
#   bash tools/make_installer_site.sh <default.xbe> [out/site]
#   TREE=<an older version's checkout, built> bash tools/make_installer_site.sh <default.xbe> [out/site]
#
# The site keeps the versions: <site>/<version>/ holds that version's locked .nro and the text files of its SD card
# folder, and versions.json lists the versions there, newest first, with what installer/versions.json says of each:
# its date and title, a warning (warn_en, warn_es) and the SD card folder it goes in (folder, SpiderMan2Xbox if not
# given).
# When the site folder doesn't exist yet it starts from what the gh-pages branch publishes, so the versions already
# there stay. The .nro of build-switch/ (made with switch/build.sh, of this repository or of TREE) is locked with
# default.xbe (tools/lock_nro.py) into its version's folder, replacing that version only; index.html is this
# repository's. The page unlocks it in the player's browser with their own default.xbe. The .nro itself is never
# published.
# Into out/release: the locked .nro as SpiderMan2Xbox-<version>.nro.locked, and SpiderMan2Xbox-<version>-symbols.elf,
# the .elf's names and debug information without its code or data (for reading crash reports' addresses).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
XBE=${1:?usage: make_installer_site.sh <default.xbe> [out dir]}
OUT=${2:-$R/out/site}
T=$(cd "${TREE:-$R}" && pwd)
VERSION=$(tr -d ' \r\n' < "$T/VERSION")
NRO=$T/build-switch/SpiderMan2Xbox.nro
[ -f "$NRO" ] || { echo "no $NRO: build it first (switch/build.sh)"; exit 1; }
if [ ! -d "$OUT" ]; then
    mkdir -p "$OUT"
    if git -C "$R" fetch -q origin gh-pages 2>/dev/null; then
        git -C "$R" archive FETCH_HEAD | tar -x -C "$OUT"
        echo "started from gh-pages: $(cd "$OUT" && ls -d */ 2>/dev/null | tr -d / | tr '\n' ' ')"
    fi
fi
V=$OUT/$VERSION
rm -rf "$V"
mkdir -p "$V"
python3 "$R/tools/lock_nro.py" lock "$NRO" "$XBE" "$V/SpiderMan2Xbox.nro.locked"
cp "$T/switch/README-switch.txt" "$V/README.txt"
cp "$T/switch/game-folder-README.txt" "$V/PUT_YOUR_GAME_HERE.txt"
{
    cat "$T/LICENSE"
    printf '\n\n'
    cat "$T/THIRD_PARTY.md"
} > "$V/LICENSES.txt"
cp "$R/installer/index.html" "$OUT/index.html"
SHA1=$(python3 -c "import hashlib,sys; print(hashlib.sha1(open(sys.argv[1],'rb').read()).hexdigest())" "$XBE")
python3 - "$OUT" "$R/installer/versions.json" "$SHA1" <<'EOF'
import json, os, re, sys
out, manifest, sha1 = sys.argv[1:4]
titles = {e['version']: e for e in json.load(open(manifest, encoding='utf-8'))}
key = lambda v: tuple(int(n) for n in re.findall(r'\d+', v))
there = [d for d in os.listdir(out) if os.path.isfile(os.path.join(out, d, 'SpiderMan2Xbox.nro.locked'))]
versions = [dict(titles.get(v, {}), version=v, path=v + '/') for v in sorted(there, key=key, reverse=True)]
with open(os.path.join(out, 'versions.json'), 'w', encoding='utf-8') as f:
    json.dump({'xbe_sha1': sha1, 'versions': versions}, f, ensure_ascii=False, indent=1)
print('versions.json:', ', '.join(v['version'] for v in versions))
EOF
touch "$OUT/.nojekyll"  # served as it is, without GitHub's Jekyll
echo "site with $VERSION in $OUT:"
ls -la "$OUT" "$V"

# the release's files
REL=$R/out/release
mkdir -p "$REL"
cp "$V/SpiderMan2Xbox.nro.locked" "$REL/SpiderMan2Xbox-$VERSION.nro.locked"
OBJCOPY=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-objcopy
READELF=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-readelf
SYM=$REL/SpiderMan2Xbox-$VERSION-symbols.elf
"$OBJCOPY" --only-keep-debug "$T/build-switch/SpiderMan2Xbox.elf" "$SYM"
# every section loaded into memory (code, data) must be left empty (NOBITS): only names, debug information and the
# build id (a note, to match a crash report to its build) go out
if "$READELF" -S -W "$SYM" | awk '$0 ~ /^ *\[ *[0-9]+\]/ { sub(/^ *\[ *[0-9]+\] */, ""); if ($2 != "NOBITS" && $2 != "NULL" && $2 != "NOTE" && $7 ~ /A/) { print; bad = 1 } } END { exit !bad }'; then
    echo "$SYM still holds code or data: not releasing it"
    rm -f "$SYM"
    exit 1
fi
echo "release files in $REL:"
ls -la "$REL"
