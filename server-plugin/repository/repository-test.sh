#!/usr/bin/env bash
# Installs every plugin of a built plugin repository the way a server owner
# does (Dashboard -> Plugins -> Repositories, then the catalog) on a throwaway
# stock Jellyfin 12.2, and checks that each loads. Never point this at a real
# server.
#
#   server-plugin/repository/repository-test.sh <dist dir with the plugin zips>
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
DIST="$(cd "${1:?dist directory}" && pwd)"
IMAGE="${JFD_TEST_IMAGE:-jellyfin/jellyfin:12.2.20261005-225228@sha256:357724bf0ae27a672c7cbaa899db2d9abeb13dbd8657ccce750258a4c059d037}"
NAME="${JFD_TEST_CONTAINER:-jfd-plugin-repo-test}"
PORT="${JFD_TEST_PORT:-18098}"
SERVE_PORT="${JFD_SERVE_PORT:-18099}"
URL="http://127.0.0.1:${PORT}"
DOCKER="${DOCKER:-docker}"
AUTH='MediaBrowser Client="repo-test", Device="ci", DeviceId="repo-test", Version="1.0"'

log() { echo "[plugin-repo] $*" >&2; }
fail() { log "FAIL: $*"; $DOCKER logs --tail 80 "$NAME" >&2 || true; exit 1; }
api() {
  local args=(-fsS -X "$1" "$URL$2" -H "Authorization: $AUTH${TOKEN:+, Token=\"$TOKEN\"}")
  [[ -n "${3:-}" ]] && args+=(-H 'Content-Type: application/json' -d "$3")
  curl "${args[@]}"
}
wait_for() {
  for ((i = 0; i < $2; i++)); do
    if eval "$3" >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  fail "timed out waiting for $1"
}
server_pid=""
work="$(mktemp -d)"
cleanup() {
  $DOCKER rm -f "$NAME" >/dev/null 2>&1 || true
  [[ -n "$server_pid" ]] && kill "$server_pid" 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

# The repository as the server will see it: the zips and a manifest that
# points at them on this machine.
cp "$DIST"/*.zip "$work/"
python3 "$here/manifest.py" "http://host.docker.internal:${SERVE_PORT}" "$work" > "$work/manifest.json"
(cd "$work" && exec python3 -m http.server "$SERVE_PORT" --bind 0.0.0.0 >/dev/null 2>&1) &
server_pid=$!

$DOCKER rm -f "$NAME" >/dev/null 2>&1 || true
log "starting $IMAGE"
$DOCKER run -d --name "$NAME" --add-host=host.docker.internal:host-gateway -p "127.0.0.1:${PORT}:8096" "$IMAGE" >/dev/null
wait_for "the server" 120 "curl -fsS $URL/System/Info/Public"
wait_for "the startup wizard" 60 "api GET /Startup/User"
api POST /Startup/Configuration '{"UICulture":"en-US","MetadataCountryCode":"US","PreferredMetadataLanguage":"en"}' >/dev/null
api POST /Startup/User '{"Name":"admin","Password":"admin"}' >/dev/null
api POST /Startup/Complete >/dev/null
TOKEN=$(api POST /Users/AuthenticateByName '{"Username":"admin","Pw":"admin"}' | jq -r .AccessToken)

log "adding the repository"
api POST /Repositories "[{\"Name\":\"test\",\"Url\":\"http://host.docker.internal:${SERVE_PORT}/manifest.json\",\"Enabled\":true}]" >/dev/null
wait_for "the catalog" 60 "api GET /Packages | jq -e 'length >= 1'"
count=$(jq length "$work/manifest.json")
for ((i = 0; i < count; i++)); do
  name=$(jq -r ".[$i].name" "$work/manifest.json")
  guid=$(jq -r ".[$i].guid" "$work/manifest.json")
  version=$(jq -r ".[$i].versions[0].version" "$work/manifest.json")
  api GET /Packages | jq -e --arg g "${guid//-/}" '.[] | select((.guid | gsub("-";"")) == $g)' >/dev/null || fail "$name is not in the catalog"
  log "installing $name $version from the catalog"
  api POST "/Packages/Installed/$(jq -rn --arg n "$name" '$n|@uri')?assemblyGuid=$guid&version=$version" >/dev/null
done
# Installs run in the background; each lands in its own plugins folder.
wait_for "the downloads" 120 \
  "[[ \$($DOCKER exec $NAME sh -c 'ls -d /config/plugins/*/ 2>/dev/null | grep -vc configurations') -ge $count ]]"
$DOCKER restart "$NAME" >/dev/null
wait_for "the server after the restart" 120 "curl -fsS $URL/System/Info/Public"
wait_for "the plugin list" 60 "api GET /Plugins"
for ((i = 0; i < count; i++)); do
  name=$(jq -r ".[$i].name" "$work/manifest.json")
  version=$(jq -r ".[$i].versions[0].version" "$work/manifest.json")
  api GET /Plugins | jq -e --arg n "$name" --arg v "$version" \
    '.[] | select(.Name == $n and .Version == $v and .Status == "Active")' >/dev/null \
    || fail "$name $version is not active: $(api GET /Plugins)"
  log "$name $version active"
done
log "PASS"
