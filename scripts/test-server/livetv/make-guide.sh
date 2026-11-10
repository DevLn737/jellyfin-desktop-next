#!/usr/bin/env bash
# Writes the Live TV stand's tuner playlist and guide to <dir>:
#   channels.m3u  <count> channels, tvg-id jfd.<n>, all served by tuner.pl on
#                 127.0.0.1:<port> inside the container, each at an address
#                 of its own (the server tells M3U channels apart by it):
#                 stream-1.ts for odd channels, stream-2.ts for even ones
#   guide.xml     XMLTV: half-hour programmes for every channel from the
#                 start of the previous hour for 25 hours, so "now" and
#                 "next" exist for the whole day the stand runs
# No external URLs: everything points at the container itself.
#
#   make-guide.sh <dir> <port> [count]
set -euo pipefail

dir="$1" port="$2" count="${3:-50}"
mkdir -p "$dir"

{
  echo '#EXTM3U'
  for ((c = 1; c <= count; c++)); do
    stream=$(( (c - 1) % 2 + 1 ))
    echo "#EXTINF:-1 tvg-id=\"jfd.$c\" tvg-name=\"Test Channel $c\" tvg-chno=\"$c\",Test Channel $c"
    echo "http://127.0.0.1:$port/stream-$stream/channel-$c.ts"
  done
} > "$dir/channels.m3u"

now=$(date -u +%s)
first=$(( now / 3600 * 3600 - 3600 ))
slots=50
# The slot times are the same for every channel: computed once.
froms=() tos=() hhmms=()
for ((s = 0; s < slots; s++)); do
  start=$(( first + s * 1800 ))
  froms+=("$(date -u -d "@$start" +%Y%m%d%H%M%S)")
  tos+=("$(date -u -d "@$(( start + 1800 ))" +%Y%m%d%H%M%S)")
  hhmms+=("$(date -u -d "@$start" +%H:%M)")
done
{
  echo '<?xml version="1.0" encoding="UTF-8"?>'
  echo '<tv generator-info-name="jfd-test-stand">'
  for ((c = 1; c <= count; c++)); do
    echo "  <channel id=\"jfd.$c\"><display-name>Test Channel $c</display-name></channel>"
  done
  for ((c = 1; c <= count; c++)); do
    for ((s = 0; s < slots; s++)); do
      echo "  <programme start=\"${froms[s]} +0000\" stop=\"${tos[s]} +0000\" channel=\"jfd.$c\">"
      echo "    <title lang=\"en\">Channel $c at ${hhmms[s]}</title>"
      echo "    <desc lang=\"en\">Test programme $s of channel $c.</desc>"
      echo "  </programme>"
    done
  done
  echo '</tv>'
} > "$dir/guide.xml"
