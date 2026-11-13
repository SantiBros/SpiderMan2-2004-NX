#!/bin/bash
# build.sh: SpiderMan2Xbox.nro from generated/game, the code xbe_recomp made from the player's own default.xbe.
#   env: DEVKITPRO (/opt/devkitpro), JOBS (nproc), OUT (build-switch)
# Needs devkitA64, libnx, switch-tools and switch-mesa (the GPU renderer's OpenGL). The .nro holds code derived from
# the game: it is for the person who owns that copy, never to be shared.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
DKP=${DEVKITPRO:-/opt/devkitpro}
export DEVKITPRO=$DKP PATH=$DKP/devkitA64/bin:$DKP/tools/bin:$PATH
GEN=$R/generated/game
OUT=${OUT:-$R/build-switch}
JOBS=${JOBS:-$(nproc)}
VERSION=${VERSION:-$(tr -d ' \r\n' < "$R/VERSION")}
CXX=aarch64-none-elf-g++
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
DEFS="-D__SWITCH__ -DXBE_SEGMENTED=1 -DXBE_RAW_VERTICES=0 -DXBE_RENDER_THREAD=0"
BASE="-std=gnu++20 $ARCH $DEFS -fno-strict-aliasing -ffunction-sections -fdata-sections"
INC="-isystem $DKP/libnx/include -I$R/sdk/runtime -I$GEN"
PORTLIBS=$DKP/portlibs/switch
[ -f "$GEN/x86_func_mapping.cpp" ] || { echo "no recompiled code in $GEN: run xbe_recomp on your default.xbe first"; exit 1; }
[ -f "$PORTLIBS/lib/libEGL.a" ] || { echo "OpenGL for the Switch is missing: install it with  sudo dkp-pacman -S switch-mesa"; exit 1; }
mkdir -p "$OUT/gen" "$OUT/host"

# recompiled code: each partition rebuilt when it is newer than its object
export CXX BASE INC GEN OUT
[ -n "$SKIP_GEN" ] || ls "$GEN"/x86_recomp.*.cpp "$GEN"/x86_func_mapping.cpp | xargs -P "$JOBS" -I{} sh -c '
    o="$OUT/gen/$(basename {} .cpp).o"; [ "$o" -nt {} ] && exit 0
    $CXX $BASE -O2 -g0 -w $INC -c {} -o "$o.tmp" && mv "$o.tmp" "$o" || { echo "FAILED {}"; exit 1; }'

# host side: the SDK runtime without its Windows parts, the game's hooks, the GPU renderer, the harness CPU renderer,
# the Switch files
SHA=$(sha1sum "$R/assets/default.xbe" 2>/dev/null | cut -c1-40)
CODE=$(python3 "$R/tools/lock_nro.py" code "$R/assets/default.xbe" 2>/dev/null)
printf 'extern const char kImageSha1[] = "%s";\nextern const char kImageCode[] = "%s";\n' "${SHA:-unknown}" "${CODE:-unknown}" > "$OUT/xbe_sha1.cpp.new"
cmp -s "$OUT/xbe_sha1.cpp.new" "$OUT/xbe_sha1.cpp" && rm "$OUT/xbe_sha1.cpp.new" || mv "$OUT/xbe_sha1.cpp.new" "$OUT/xbe_sha1.cpp"
# the patches that make other editions' default.xbe into this one (assets/editions/*.patch, tools/edition_patch.py),
# carried in the .nro, which is only published locked
python3 - "$R/assets/editions" "$OUT/editions.cpp.new" <<'PY'
import glob, os, sys
src, out = sys.argv[1:3]
names = sorted(glob.glob(os.path.join(src, '*.patch')))
with open(out, 'w') as f:
    f.write('#include <cstddef>\n#include <cstdint>\n')
    for i, n in enumerate(names):
        data = open(n, 'rb').read()
        f.write('alignas(8) static const uint8_t kPatch%d[] = {' % i)
        f.write(','.join(str(b) for b in data))
        f.write('};\n')
    f.write('struct EditionBlob { const uint8_t* data; size_t size; };\n')
    f.write('extern const EditionBlob kEditionPatches[] = {%s};\n' % ''.join('{kPatch%d, sizeof kPatch%d},' % (i, i) for i in range(len(names))) if names else 'extern const EditionBlob kEditionPatches[] = {{nullptr, 0}};\n')
    f.write('extern const size_t kEditionPatchCount = %d;\n' % len(names))
PY
cmp -s "$OUT/editions.cpp.new" "$OUT/editions.cpp" && rm "$OUT/editions.cpp.new" || mv "$OUT/editions.cpp.new" "$OUT/editions.cpp"
SRC=$(ls "$R"/sdk/runtime/*/*.cpp | grep -Ev "/runtime/app/|/core/input.cpp|/gpu/d3d12.cpp|/audio/xaudio2.cpp")
SRC="$SRC $(ls "$R"/src/*/*.cpp) $(ls "$R"/gl/*.cpp) $R/sdk/harness/reference_renderer.cpp"
SRC="$SRC $(ls "$R"/switch/*.cpp) $OUT/xbe_sha1.cpp $OUT/editions.cpp"
HOSTINC="$INC -isystem $PORTLIBS/include -I$R/src -I$R -I$R/sdk/harness -I$R/sdk/harness/compat"
newest=$( (find "$R/sdk/runtime" "$R/src" "$R/gl" "$R/switch" "$R/sdk/harness" "$GEN/image_symbols.h" -name '*.h' -printf '%T@ %p\n'; find "$R/VERSION" -printf '%T@ %p\n') | sort -n | tail -1 | cut -d' ' -f2)
objs=()
for f in $SRC; do
    o=$OUT/host/$(echo "${f#$R/}" | tr / _ | sed 's/\.cpp$/.o/'); objs+=("$o")
    [ "$o" -nt "$f" ] && [ "$o" -nt "$newest" ] && continue
    echo "compile ${f#$R/}"
    $CXX $BASE -O2 -g $HOSTINC -DSM2NX_VERSION="\"$VERSION\"" -c "$f" -o "$o" &
    [ "$(jobs -r | wc -l)" -ge "$JOBS" ] && wait -n
done
wait

[ -n "$NO_LINK" ] && exit 0
echo "link"
$CXX -specs=$DKP/libnx/switch.specs $ARCH -Wl,--gc-sections -Wl,-Map,"$OUT/SpiderMan2Xbox.map" -o "$OUT/SpiderMan2Xbox.elf" \
    "${objs[@]}" "$OUT"/gen/*.o -L$PORTLIBS/lib -L$DKP/libnx/lib -lEGL -lglapi -ldrm_nouveau -lnx
nacptool --create "Spider-Man 2 (Xbox)" "SantiBros" "$VERSION" "$OUT/SpiderMan2Xbox.nacp"
elf2nro "$OUT/SpiderMan2Xbox.elf" "$OUT/SpiderMan2Xbox.nro" --icon="$R/switch/icon.jpg" --nacp="$OUT/SpiderMan2Xbox.nacp"
ls -l "$OUT/SpiderMan2Xbox.nro"
