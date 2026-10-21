#!/bin/bash
# build.sh: SpiderMan2Xbox.nro from generated/game, the code xbe_recomp made from the player's own default.xbe.
#   env: DEVKITPRO (/opt/devkitpro), JOBS (nproc), OUT (build-switch)
# Needs devkitA64, libnx and switch-tools. The .nro holds code derived from the game: it is for the person who
# owns that copy, never to be shared.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
DKP=${DEVKITPRO:-/opt/devkitpro}
export DEVKITPRO=$DKP PATH=$DKP/devkitA64/bin:$DKP/tools/bin:$PATH
GEN=$R/generated/game
OUT=${OUT:-$R/build-switch}
JOBS=${JOBS:-$(nproc)}
VERSION=${VERSION:-0.0.7}
CXX=aarch64-none-elf-g++
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
DEFS="-D__SWITCH__ -DXBE_SEGMENTED=1 -DXBE_RAW_VERTICES=0 -DXBE_RENDER_THREAD=0"
BASE="-std=gnu++20 $ARCH $DEFS -fno-strict-aliasing -ffunction-sections -fdata-sections"
INC="-isystem $DKP/libnx/include -I$R/sdk/runtime -I$GEN"
[ -f "$GEN/x86_func_mapping.cpp" ] || { echo "no recompiled code in $GEN: run xbe_recomp on your default.xbe first"; exit 1; }
mkdir -p "$OUT/gen" "$OUT/host"

# recompiled code: each partition rebuilt when it is newer than its object
export CXX BASE INC GEN OUT
[ -n "$SKIP_GEN" ] || ls "$GEN"/x86_recomp.*.cpp "$GEN"/x86_func_mapping.cpp | xargs -P "$JOBS" -I{} sh -c '
    o="$OUT/gen/$(basename {} .cpp).o"; [ "$o" -nt {} ] && exit 0
    $CXX $BASE -O2 -g0 -w $INC -c {} -o "$o.tmp" && mv "$o.tmp" "$o" || { echo "FAILED {}"; exit 1; }'

# host side: the SDK runtime without its Windows parts, the game's hooks, the harness CPU renderer, the Switch files
SHA=$(sha1sum "$R/assets/default.xbe" 2>/dev/null | cut -c1-40)
echo "extern const char kImageSha1[] = \"${SHA:-unknown}\";" > "$OUT/xbe_sha1.cpp"
SRC=$(ls "$R"/sdk/runtime/*/*.cpp | grep -Ev "/runtime/app/|/core/input.cpp|/gpu/d3d12.cpp|/audio/xaudio2.cpp")
SRC="$SRC $(ls "$R"/src/*/*.cpp) $R/sdk/harness/reference_renderer.cpp"
SRC="$SRC $(ls "$R"/switch/*.cpp) $OUT/xbe_sha1.cpp"
HOSTINC="$INC -I$R/src -I$R -I$R/sdk/harness -I$R/sdk/harness/compat"
newest=$(find "$R/sdk/runtime" "$R/src" "$R/switch" "$R/sdk/harness" "$GEN/image_symbols.h" -name '*.h' -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2)
objs=()
for f in $SRC; do
    o=$OUT/host/$(echo "${f#$R/}" | tr / _ | sed 's/\.cpp$/.o/'); objs+=("$o")
    [ "$o" -nt "$f" ] && [ "$o" -nt "$newest" ] && continue
    echo "compile ${f#$R/}"
    $CXX $BASE -O2 -g $HOSTINC -c "$f" -o "$o" &
    [ "$(jobs -r | wc -l)" -ge "$JOBS" ] && wait -n
done
wait

[ -n "$NO_LINK" ] && exit 0
echo "link"
$CXX -specs=$DKP/libnx/switch.specs $ARCH -Wl,--gc-sections -Wl,-Map,"$OUT/SpiderMan2Xbox.map" -o "$OUT/SpiderMan2Xbox.elf" \
    "${objs[@]}" "$OUT"/gen/*.o -L$DKP/libnx/lib -lnx
nacptool --create "Spider-Man 2 (Xbox) test" "SantiBros" "$VERSION" "$OUT/SpiderMan2Xbox.nacp"
elf2nro "$OUT/SpiderMan2Xbox.elf" "$OUT/SpiderMan2Xbox.nro" --icon="$R/switch/icon.jpg" --nacp="$OUT/SpiderMan2Xbox.nacp"
ls -l "$OUT/SpiderMan2Xbox.nro"
