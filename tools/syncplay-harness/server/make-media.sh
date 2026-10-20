#!/bin/sh
# Generates the free, synthetic test library. Every frame carries a 16-bit
# barcode of its frame number (top-left, 16 cells of 40x40 px, least
# significant bit first, white = 1) so any client's on-screen position can be
# read from a screenshot. 24 fps, VP9 + Opus in WebM so that open-source
# Chromium (no H.264/AAC) direct-plays it.
set -eu
out="${1:-/media}"
dir="$out/movies/SyncPlay Test Clip (2026)"
mkdir -p "$dir"
duration="${CLIP_SECONDS:-600}"
"${FFMPEG:-ffmpeg}" -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=640x360:rate=24:duration=${duration}" \
  -f lavfi -i "color=c=black:size=640x40:rate=24:duration=${duration}" \
  -f lavfi -i "sine=frequency=440:beep_factor=4:sample_rate=48000:duration=${duration}" \
  -filter_complex "[1:v]format=gray,geq=lum='255*mod(floor(N/pow(2,floor(X/40))),2)'[bar];[0:v][bar]overlay=0:0,drawtext=text='%{pts\:hms} f=%{n}':x=10:y=50:fontsize=24:fontcolor=white:box=1:boxcolor=black[v]" \
  -map "[v]" -map 2:a -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -row-mt 1 -b:v 400k -g 48 \
  -c:a libopus -b:a 64k "$dir/SyncPlay Test Clip (2026).webm"
