#!/bin/bash
# make_reel.sh <rec.mp4> <out.mp4> <start:length> ...
# The README's video: pieces of a recording of the PC test build, joined with short fades, as an H.264 MP4 that
# plays in a browser (docs/images/clips/gameplay.mp4), 720x540 and under 10 MB: the size GitHub takes for a video
# dropped into a README or an issue, where it plays inline.
set -euo pipefail
in=$1 out=$2
shift 2
n=0 filter="" inputs=()
for part in "$@"; do
    s=${part%%:*} l=${part##*:}
    inputs+=(-ss "$s" -t "$l" -i "$in")
    fo=$(awk -v l="$l" 'BEGIN { printf "%.2f", l - 0.4 }')
    filter+="[$n:v]fps=30,scale=720:540:flags=lanczos,fade=t=in:d=0.4,fade=t=out:st=$fo:d=0.4,setpts=PTS-STARTPTS[v$n];"
    n=$((n + 1))
done
for ((i = 0; i < n; i++)); do filter+="[v$i]"; done
filter+="concat=n=$n:v=1:a=0[v]"
ffmpeg -loglevel error -y "${inputs[@]}" -filter_complex "$filter" -map "[v]" -c:v libx264 -preset slow -crf 29 \
    -pix_fmt yuv420p -movflags +faststart "$out"
ls -l "$out"
