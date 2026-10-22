#!/bin/bash
# make_clip.sh <rec.mp4> <start s> <length s> <out name> [width] [fps]
# A README clip from a recording of the PC test build (GL=1 TIMESCALE=0.15 GLREC="from to", see docs/BUILDING.md):
# docs/images/clips/<name>.webp, an animated WebP that plays in the README (a GIF of the same clip is about five
# times bigger). The recording is the game's 4:3 picture; the clip keeps it.
set -euo pipefail
in=$1 start=$2 len=$3 name=$4 width=${5:-720} fps=${6:-20}
out=$(cd "$(dirname "$0")/../.." && pwd)/docs/images/clips
mkdir -p "$out"
ffmpeg -loglevel error -y -ss "$start" -t "$len" -i "$in" -an -vf "fps=$fps,scale=$width:-1:flags=lanczos" \
    -c:v libwebp -lossless 0 -q:v 70 -compression_level 6 -loop 0 "$out/$name.webp"
ls -l "$out/$name.webp"
