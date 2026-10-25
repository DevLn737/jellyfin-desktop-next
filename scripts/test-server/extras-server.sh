#!/usr/bin/env bash
# A second throwaway Jellyfin 12.2 for the player checks that would upset the
# shared stand's item and track counts (stock-server.sh): a movie with an
# external audio file, and media segments (intro, credits) from a test
# provider plugin (segments-plugin/). Prints the environment for the extras
# tests. Never point this at a real server.
#
#   scripts/test-server/extras-server.sh up      # start and print env
#   scripts/test-server/extras-server.sh down    # remove the container
set -euo pipefail

IMAGE="${JFD_TEST_IMAGE:-jellyfin/jellyfin:12.2.20261005-225228@sha256:357724bf0ae27a672c7cbaa899db2d9abeb13dbd8657ccce750258a4c059d037}"
NAME="${JFD_EXTRAS_CONTAINER:-jfd-extras-server}"
PORT="${JFD_EXTRAS_PORT:-18097}"
USER_NAME="jfd-test"
PASSWORD="jfd-test-password"
URL="http://127.0.0.1:${PORT}"
DOCKER="${DOCKER:-docker}"
AUTH='MediaBrowser Client="jfd-test-setup", Device="ci", DeviceId="jfd-extras-setup", Version="0.0.1"'
HERE="$(cd "$(dirname "$0")" && pwd)"

log() { echo "[extras-server] $*" >&2; }

api() {
  local method="$1" path="$2" body="${3:-}" token="${4:-}"
  local header="$AUTH"
  [[ -n "$token" ]] && header="$AUTH, Token=\"$token\""
  if [[ -n "$body" ]]; then
    curl -fsS -X "$method" "$URL$path" -H "Authorization: $header" -H 'Content-Type: application/json' -d "$body"
  else
    curl -fsS -X "$method" "$URL$path" -H "Authorization: $header"
  fi
}

wait_for() {
  local what="$1" seconds="$2"; shift 2
  for ((i = 0; i < seconds; i++)); do
    if "$@" >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  log "timed out waiting for $what"
  $DOCKER logs --tail 50 "$NAME" >&2 || true
  return 1
}

make_media() {
  # A movie with one embedded audio track (jpn), an external one (rus,
  # Movie.ru.mka), two external subtitles (rus .srt, eng .ass) and
  # segments: intro 2-12 s, credits 50-58 s.
  $DOCKER exec "$NAME" bash -euc '
    ff=/usr/lib/jellyfin-ffmpeg/ffmpeg
    dir="/media/Movies/Extras Movie (2024)"
    mkdir -p "$dir"
    $ff -loglevel error -y -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=frequency=440 -t 60 \
      -c:v libx264 -preset ultrafast -c:a aac -metadata:s:a:0 language=jpn "$dir/Extras Movie (2024).mkv"
    $ff -loglevel error -y -f lavfi -i sine=frequency=880 -t 60 -c:a libopus -b:a 32k \
      -metadata:s:a:0 language=rus "$dir/Extras Movie (2024).ru.mka"
    cat > "$dir/Extras Movie (2024).ru.srt" <<SRT
1
00:00:01,000 --> 00:00:59,000
Привет
SRT
    cat > "$dir/Extras Movie (2024).en.ass" <<ASS
[Script Info]
ScriptType: v4.00+

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:59.00,Default,,0,0,0,,Hello
ASS
    cat > "$dir/Extras Movie (2024).segments.json" <<JSON
[{"Type": "Intro", "Start": 2, "End": 12}, {"Type": "Outro", "Start": 50, "End": 58}]
JSON
  '
}

up() {
  $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true
  log "building the segment provider"
  local plugin
  plugin="$(mktemp -d)"
  "$HERE/segments-plugin/build.sh" "$IMAGE" "$plugin/JFD Test Segments_1.0.0.0"
  log "starting $IMAGE on port $PORT"
  $DOCKER create --name "$NAME" -p "127.0.0.1:${PORT}:8096" "$IMAGE" >/dev/null
  $DOCKER cp "$plugin/." "$NAME:/config/plugins/" >/dev/null
  rm -rf "$plugin"
  $DOCKER start "$NAME" >/dev/null
  wait_for "the server" 120 curl -fsS "$URL/System/Info/Public"
  make_media

  log "completing the startup wizard"
  wait_for "the startup wizard" 60 api GET /Startup/User
  api POST /Startup/Configuration '{"UICulture":"en-US","MetadataCountryCode":"US","PreferredMetadataLanguage":"en"}' >/dev/null
  api POST /Startup/User "{\"Name\":\"$USER_NAME\",\"Password\":\"$PASSWORD\"}" >/dev/null
  api POST /Startup/RemoteAccess '{"EnableRemoteAccess":true}' >/dev/null
  api POST /Startup/Complete >/dev/null

  local token
  token=$(api POST /Users/AuthenticateByName "{\"Username\":\"$USER_NAME\",\"Pw\":\"$PASSWORD\"}" | jq -r .AccessToken)
  if ! api GET /Plugins '' "$token" | jq -e '.[] | select(.Name == "JFD Test Segments" and .Status == "Active")' >/dev/null; then
    log "the segment provider did not load"
    api GET /Plugins '' "$token" >&2 || true
    $DOCKER logs --tail 50 "$NAME" >&2 || true
    return 1
  fi
  log "adding the library"
  api POST "/Library/VirtualFolders?name=Movies&collectionType=movies&paths=%2Fmedia%2FMovies&refreshLibrary=false" \
    '{"LibraryOptions":{"EnableRealtimeMonitor":false}}' "$token" >/dev/null
  api POST /Library/Refresh '' "$token" >/dev/null
  local query="$URL/Items?recursive=true&includeItemTypes=Movie"
  wait_for "the library scan" 180 bash -c \
    "[[ \$(curl -fsS '$query' -H 'Authorization: $AUTH, Token=\"$token\"' | jq .TotalRecordCount) -ge 1 ]]"
  local id
  id=$(api GET "/Items?recursive=true&includeItemTypes=Movie" '' "$token" | jq -r '.Items[0].Id')
  # The providers run in their own scheduled task, not in the scan.
  log "extracting media segments"
  local task
  task=$(api GET /ScheduledTasks '' "$token" | jq -r '.[] | select(.Key == "TaskExtractMediaSegments") | .Id')
  api POST "/ScheduledTasks/Running/$task" '' "$token" >/dev/null
  wait_for "the movie's segments" 120 bash -c \
    "[[ \$(curl -fsS '$URL/MediaSegments/$id' -H 'Authorization: $AUTH, Token=\"$token\"' | jq '.Items | length') -ge 2 ]]"
  wait_for "the external audio" 60 bash -c \
    "[[ \$(curl -fsS '$URL/Items/$id/PlaybackInfo' -X POST -H 'Content-Type: application/json' -d '{}' -H 'Authorization: $AUTH, Token=\"$token\"' | jq '[.MediaSources[0].MediaStreams[] | select(.Type == \"Audio\")] | length') -ge 2 ]]"
  wait_for "the external subtitles" 60 bash -c \
    "[[ \$(curl -fsS '$URL/Items/$id/PlaybackInfo' -X POST -H 'Content-Type: application/json' -d '{}' -H 'Authorization: $AUTH, Token=\"$token\"' | jq '[.MediaSources[0].MediaStreams[] | select(.Type == \"Subtitle\" and .IsExternal)] | length') -ge 2 ]]"
  api POST /Sessions/Logout '' "$token" >/dev/null || true

  log "ready"
  echo "JFD_EXTRAS_SERVER_URL=$URL"
  echo "JFD_EXTRAS_USER=$USER_NAME"
  echo "JFD_EXTRAS_PASSWORD=$PASSWORD"
}

down() { $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true; }

case "${1:-up}" in
  up) up ;;
  down) down ;;
  *) echo "usage: $0 up|down" >&2; exit 2 ;;
esac
