#!/usr/bin/env bash
# Takes the README screenshots (docs/screenshots) from the UI tests: the demo
# catalogue of tests/ui/ShowcaseScreens.cpp and the README cases of the
# player, rooms and music tests. Every picture is drawn by the tests; no
# third-party artwork.
#
#   scripts/readme-screenshots.sh <build dir>
#
# The build needs the app and UI tests (-DJFD_BUILD_APP=ON with Qt found),
# a display (xvfb-run on Linux) and ImageMagick's `convert`.
set -euo pipefail

build=${1:?usage: $0 <build dir>}
repo=$(cd "$(dirname "$0")/.." && pwd)
out="$repo/docs/screenshots"
shots=$(mktemp -d)
trap 'rm -rf "$shots"' EXIT

cmake --build "$build" --target jfd_ui_showcase_tests jfd_ui_player_tests jfd_ui_rooms_tests jfd_ui_music_tests

run() {
  local test=$1 filter=$2
  JFD_UI_SCREENSHOTS="$shots" JFD_UI_LANG=ru LANG=C.UTF-8 \
    xvfb-run -a -s "-screen 0 1920x1080x24" "$build/tests/$test" ${filter:+"-tc=$filter"} >/dev/null
}
run jfd_ui_showcase_tests ""
run jfd_ui_player_tests "*README*"
run jfd_ui_rooms_tests "*README*"
run jfd_ui_music_tests "*README*"

mkdir -p "$out"
# name in the README <- file the tests save
while read -r name file; do
  convert "$shots/$file.png" -resize "1600x>" -strip -quality 88 "$out/$name.jpg"
done <<'EOF'
home home-1600
series series-1600
movie movie-1920
library library-1600-filters
player readme-player-1600
watch-together readme-rooms-1600
music readme-music-1600
settings settings-1600
EOF
ls -l "$out"
