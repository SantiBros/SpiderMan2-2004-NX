#!/usr/bin/env bash
# make_release.sh: out/SpiderMan2-NX-<version>.zip, the release's download: the repository at HEAD, which is all a
# player needs to build the port from their own copy (build.sh). No game files and nothing built from them: the
# zip is checked for both. Run it on a clean checkout of the commit to be tagged.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
cd "$R"
V=$(tr -d ' \r\n' < VERSION)
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "commit your changes first"; exit 1; }
mkdir -p out
Z="out/SpiderMan2-NX-$V.zip"
rm -f "$Z"
git archive --format=zip --prefix="SpiderMan2-NX-$V/" -o "$Z" HEAD
if unzip -l "$Z" | grep -qiE '\.(xbe|nro|elf|iso|xiso|cci|cso|pak|wbk|xmv)$|/generated/|/assets/|/build-switch/'; then
    echo "$Z holds a game file or a build of one: not releasing it"
    rm -f "$Z"
    exit 1
fi
unzip -Z1 "$Z" | grep -qx "SpiderMan2-NX-$V/build.sh" || { echo "build.sh is missing from $Z"; exit 1; }
echo "$Z: $(unzip -Z1 "$Z" | grep -vc '/$') files, $(du -h "$Z" | cut -f1)"
sha256sum "$Z"
