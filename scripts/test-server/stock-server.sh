#!/usr/bin/env bash
# Starts a throwaway stock Jellyfin 12.2 in Docker with generated test media,
# completes the startup wizard and prints the environment for
# jfd_server_tests. Never point this at a real server.
#
#   scripts/test-server/stock-server.sh up      # start and print env
#   scripts/test-server/stock-server.sh down    # remove the container
#
# The image is pinned by digest. JFD_TEST_IMAGE overrides it, for example
# with the SyncPlay-rate fork image built by the test stand.
set -euo pipefail

IMAGE="${JFD_TEST_IMAGE:-jellyfin/jellyfin:12.2.20261005-225228@sha256:357724bf0ae27a672c7cbaa899db2d9abeb13dbd8657ccce750258a4c059d037}"
NAME="${JFD_TEST_CONTAINER:-jfd-test-server}"
PORT="${JFD_TEST_PORT:-18096}"
USER_NAME="jfd-test"
PASSWORD="jfd-test-password"
URL="http://127.0.0.1:${PORT}"
DOCKER="${DOCKER:-docker}"
AUTH='MediaBrowser Client="jfd-test-setup", Device="ci", DeviceId="jfd-test-setup", Version="0.0.1"'

log() { echo "[test-server] $*" >&2; }

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
  # Generated with the image's own ffmpeg: a movie with two audio tracks, an
  # embedded and an external subtitle, and a two-episode series.
  $DOCKER exec "$NAME" bash -euc '
    ff=/usr/lib/jellyfin-ffmpeg/ffmpeg
    mkdir -p "/media/Movies/Test Movie (2024)" "/media/Shows/Test Show/Season 01"
    cat > /tmp/sub.srt <<SRT
1
00:00:01,000 --> 00:00:04,000
Hello

2
00:00:05,000 --> 00:00:08,000
World
SRT
    $ff -loglevel error -y -f lavfi -i testsrc2=size=640x360:rate=24 -f lavfi -i sine=frequency=440 \
      -f lavfi -i sine=frequency=880 -i /tmp/sub.srt -t 30 \
      -map 0:v -map 1:a -map 2:a -map 3:s -c:v libx264 -preset ultrafast -c:a aac -c:s ass \
      -metadata:s:a:0 language=jpn -metadata:s:a:1 language=rus -metadata:s:s:0 language=eng \
      "/media/Movies/Test Movie (2024)/Test Movie (2024).mkv"
    cp /tmp/sub.srt "/media/Movies/Test Movie (2024)/Test Movie (2024).ru.srt"
    # A film with external audio tracks and a forced subtitle beside it, for
    # the download tests (jfd_offline_server_tests); sorts after Test Movie.
    v="/media/Movies/Voiced Film (2024)"
    mkdir -p "$v"
    $ff -loglevel error -y -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=frequency=330 -t 20 \
      -c:v libx264 -preset ultrafast -c:a aac -b:a 64k "$v/Voiced Film (2024).mkv"
    $ff -loglevel error -y -f lavfi -i sine=frequency=550 -t 20 -c:a aac -b:a 64k "$v/Voiced Film (2024).ru.mka"
    $ff -loglevel error -y -f lavfi -i sine=frequency=660 -t 20 -c:a ac3 -b:a 96k "$v/Voiced Film (2024).en.ac3"
    cp /tmp/sub.srt "$v/Voiced Film (2024).ru.forced.srt"
    # Episodes run past the 5-minute minimum the server needs for resume points.
    for e in 01 02; do
      $ff -loglevel error -y -f lavfi -i testsrc2=size=160x120:rate=5 -f lavfi -i sine=frequency=330 \
        -t 400 -c:v libx264 -preset ultrafast -c:a aac -b:a 32k \
        "/media/Shows/Test Show/Season 01/Test Show S01E$e.mkv"
    done
  '
}

up() {
  $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true
  log "starting $IMAGE on port $PORT"
  $DOCKER run -d --name "$NAME" -p "127.0.0.1:${PORT}:8096" "$IMAGE" >/dev/null
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
  log "adding libraries"
  api POST "/Library/VirtualFolders?name=Movies&collectionType=movies&paths=%2Fmedia%2FMovies&refreshLibrary=false" \
    '{"LibraryOptions":{"EnableRealtimeMonitor":false}}' "$token" >/dev/null
  # Trickplay tiles for the episodes, for the seek-bar preview tests.
  api POST "/Library/VirtualFolders?name=Shows&collectionType=tvshows&paths=%2Fmedia%2FShows&refreshLibrary=false" \
    '{"LibraryOptions":{"EnableRealtimeMonitor":false,"EnableTrickplayImageExtraction":true}}' "$token" >/dev/null
  api POST /Library/Refresh '' "$token" >/dev/null
  # Quick Connect is on by default in 12.x; make sure for the tests.
  wait_for "the library scan" 180 bash -c \
    "[[ \$(curl -fsS '$URL/Items?recursive=true&includeItemTypes=Movie,Episode' -H 'Authorization: $AUTH, Token=\"$token\"' | jq .TotalRecordCount) -ge 3 ]]"
  # The scan has listed the items before it has probed them, and the
  # trickplay task skips an episode whose video streams are not known yet:
  # it can go Idle in two seconds having made nothing. So wait for the scan
  # task, then run the trickplay task until every episode has tiles.
  local tasks scan task
  tasks=$(api GET /ScheduledTasks '' "$token")
  scan=$(jq -r '.[] | select(.Key == "RefreshLibrary") | .Id' <<<"$tasks")
  task=$(jq -r '.[] | select(.Key == "RefreshTrickplayImages") | .Id' <<<"$tasks")
  wait_for "the library scan task" 180 bash -c \
    "[[ \$(curl -fsS '$URL/ScheduledTasks/$scan' -H 'Authorization: $AUTH, Token=\"$token\"' | jq -r .State) == Idle ]]"
  local missing=1
  for ((attempt = 1; attempt <= 5; attempt++)); do
    log "generating trickplay tiles (attempt $attempt)"
    api POST "/ScheduledTasks/Running/$task" '' "$token" >/dev/null
    sleep 2
    wait_for "trickplay tiles" 180 bash -c \
      "[[ \$(curl -fsS '$URL/ScheduledTasks/$task' -H 'Authorization: $AUTH, Token=\"$token\"' | jq -r .State) == Idle ]]"
    missing=$(api GET "/Items?recursive=true&includeItemTypes=Episode&fields=Trickplay" '' "$token" |
      jq 'if (.Items | length) == 0 then 1 else [.Items[] | select((.Trickplay // {}) | length == 0)] | length end')
    [[ "$missing" == 0 ]] && break
    log "$missing episode(s) without trickplay tiles yet"
    sleep 5
  done
  if [[ "$missing" != 0 ]]; then
    log "episodes still without trickplay tiles: $missing"
    return 1
  fi
  api POST /Sessions/Logout '' "$token" >/dev/null || true

  log "ready"
  echo "JFD_TEST_SERVER_URL=$URL"
  echo "JFD_TEST_USER=$USER_NAME"
  echo "JFD_TEST_PASSWORD=$PASSWORD"
}

down() { $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true; }

case "${1:-up}" in
  up) up ;;
  down) down ;;
  *) echo "usage: $0 up|down" >&2; exit 2 ;;
esac
