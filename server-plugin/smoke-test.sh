#!/usr/bin/env bash
# Smoke test of the Room Members plugin on a throwaway stock Jellyfin 12.2:
# two users in one SyncPlay group see each other's position and ping, a user
# outside the group sees nothing. Never point this at a real server.
#
#   server-plugin/smoke-test.sh <dir with Jellyfin.Plugin.RoomMembers.dll>
set -euo pipefail
PLUGIN_DIR="$(cd "${1:?plugin build directory}" && pwd)"
IMAGE="${JFD_TEST_IMAGE:-jellyfin/jellyfin:12.2.20261005-225228@sha256:357724bf0ae27a672c7cbaa899db2d9abeb13dbd8657ccce750258a4c059d037}"
NAME="${JFD_TEST_CONTAINER:-jfd-room-members-test}"
PORT="${JFD_TEST_PORT:-18097}"
URL="http://127.0.0.1:${PORT}"
DOCKER="${DOCKER:-docker}"

log() { echo "[room-members] $*" >&2; }
fail() { log "FAIL: $*"; $DOCKER logs --tail 80 "$NAME" >&2 || true; exit 1; }
auth() { echo "MediaBrowser Client=\"smoke\", Device=\"$1\", DeviceId=\"$1\", Version=\"1.0\"${2:+, Token=\"$2\"}"; }
# api METHOD PATH DEVICE [TOKEN] [BODY]
api() {
  local args=(-fsS -X "$1" "$URL$2" -H "Authorization: $(auth "$3" "${4:-}")")
  [[ -n "${5:-}" ]] && args+=(-H 'Content-Type: application/json' -d "$5")
  curl "${args[@]}"
}
wait_for() {
  for ((i = 0; i < $2; i++)); do
    if eval "$3" >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  fail "timed out waiting for $1"
}
cleanup() { $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT

cleanup
log "starting $IMAGE with the plugin"
$DOCKER run -d --name "$NAME" -p "127.0.0.1:${PORT}:8096" "$IMAGE" >/dev/null
$DOCKER exec "$NAME" mkdir -p /config/plugins/RoomMembers_1.0.0.0
$DOCKER cp "$PLUGIN_DIR/Jellyfin.Plugin.RoomMembers.dll" "$NAME:/config/plugins/RoomMembers_1.0.0.0/"
$DOCKER restart "$NAME" >/dev/null
wait_for "the server" 120 "curl -fsS $URL/System/Info/Public"
$DOCKER exec "$NAME" bash -euc '
  mkdir -p "/media/Movies/Clip (2024)"
  /usr/lib/jellyfin-ffmpeg/ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=160x120:rate=5 -t 600 \
    -c:v libx264 -preset ultrafast "/media/Movies/Clip (2024)/Clip (2024).mkv"'

wait_for "the startup wizard" 60 "api GET /Startup/User setup"
api POST /Startup/Configuration setup '' '{"UICulture":"en-US","MetadataCountryCode":"US","PreferredMetadataLanguage":"en"}' >/dev/null
api POST /Startup/User setup '' '{"Name":"alice","Password":"alice"}' >/dev/null
api POST /Startup/Complete setup >/dev/null
login() { api POST /Users/AuthenticateByName "$1" '' "{\"Username\":\"$2\",\"Pw\":\"$2\"}" | jq -r .AccessToken; }
alice=$(login alice-pc alice)
api POST /Users/New alice-pc "$alice" '{"Name":"bob","Password":"bob"}' >/dev/null
api POST /Users/New alice-pc "$alice" '{"Name":"carol","Password":"carol"}' >/dev/null
bob=$(login bob-pc bob)
carol=$(login carol-pc carol)
api POST "/Library/VirtualFolders?name=Movies&collectionType=movies&paths=%2Fmedia%2FMovies&refreshLibrary=true" \
  alice-pc "$alice" '{"LibraryOptions":{"EnableRealtimeMonitor":false}}' >/dev/null
wait_for "the library scan" 180 \
  "[[ \$(api GET '/Items?recursive=true&includeItemTypes=Movie' alice-pc $alice | jq .TotalRecordCount) -ge 1 ]]"
item=$(api GET '/Items?recursive=true&includeItemTypes=Movie' alice-pc "$alice" | jq -r '.Items[0].Id')

log "plugin loaded"
api GET /Plugins alice-pc "$alice" | jq -e '.[] | select(.Name == "Room Members" and .Status == "Active")' >/dev/null \
  || fail "plugin not active: $(api GET /Plugins alice-pc "$alice")"

log "outside a group: no members"
api GET /RoomMembers alice-pc "$alice" | jq -e '.GroupId == null and (.Members | length) == 0' >/dev/null \
  || fail "unexpected answer outside a group"

log "alice creates a group, bob joins"
api POST /SyncPlay/New alice-pc "$alice" '{"GroupName":"smoke"}' >/dev/null
group=$(api GET /SyncPlay/List alice-pc "$alice" | jq -r '.[0].GroupId')
api POST /SyncPlay/Join bob-pc "$bob" "{\"GroupId\":\"$group\"}" >/dev/null
api POST /SyncPlay/Ping alice-pc "$alice" '{"Ping":42}' >/dev/null
api POST /SyncPlay/Ping bob-pc "$bob" '{"Ping":137}' >/dev/null
api POST /Sessions/Playing bob-pc "$bob" "{\"ItemId\":\"$item\",\"PositionTicks\":1230000000,\"IsPaused\":false}" >/dev/null
api POST /Sessions/Playing/Progress bob-pc "$bob" "{\"ItemId\":\"$item\",\"PositionTicks\":1250000000,\"IsPaused\":true}" >/dev/null

answer=$(api GET /RoomMembers alice-pc "$alice")
echo "$answer" | jq . >&2
echo "$answer" | jq -e --arg g "$group" '(.GroupId | gsub("-";"")) == ($g | gsub("-";"")) and (.Members | length) == 2' >/dev/null \
  || fail "group or member count"
echo "$answer" | jq -e '.Members[] | select(.UserName == "alice") | .IsSelf == true and .PingMs == 42 and .PositionTicks == null' >/dev/null \
  || fail "alice's own entry"
echo "$answer" | jq -e '.Members[] | select(.UserName == "bob") | .IsSelf == false and .PingMs == 137 and .PositionTicks == 1250000000 and .IsPaused == true and .PositionUpdatedUtc != null and .DeviceName == "bob-pc"' >/dev/null \
  || fail "bob's entry"

log "carol, outside the group, sees nothing of it"
api GET /RoomMembers carol-pc "$carol" | jq -e '.GroupId == null and (.Members | length) == 0' >/dev/null \
  || fail "a non-member saw the group"

log "after bob leaves, alice sees only herself"
api POST /SyncPlay/Leave bob-pc "$bob" >/dev/null
api GET /RoomMembers alice-pc "$alice" | jq -e '(.Members | length) == 1 and .Members[0].IsSelf' >/dev/null \
  || fail "member list after leave"
log "PASS"
