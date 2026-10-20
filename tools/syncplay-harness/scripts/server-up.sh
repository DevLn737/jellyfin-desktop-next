#!/usr/bin/env bash
# Starts the SyncPlay test server and brings it to the known state the
# harness expects. Idempotent: reuses the image and container when present.
#
#   scripts/server-up.sh               # build from the fork commits (needs Docker)
#   PAYLOAD=release scripts/server-up.sh   # use CI-built server/web payloads from
#                                          # the syncplay-test-server GitHub release
#   PAYLOAD=/path scripts/server-up.sh     # use /path/server and /path/web
#
# Environment: IMAGE, CONTAINER, PORT (default 8096), SERVER_REF, WEB_REF.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-jellyfin-syncplay-test:local}"
CONTAINER="${CONTAINER:-jfd-syncplay-test}"
PORT="${PORT:-8096}"
PAYLOAD="${PAYLOAD:-}"
REPO="${REPO:-DevLn737/jellyfin-desktop-next}"
RELEASE_TAG="${RELEASE_TAG:-syncplay-test-server}"

build_args=()
[[ -n "${SERVER_REF:-}" ]] && build_args+=(--build-arg "SERVER_REF=${SERVER_REF}")
[[ -n "${WEB_REF:-}" ]] && build_args+=(--build-arg "WEB_REF=${WEB_REF}")

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  if [[ "$PAYLOAD" == release ]]; then
    PAYLOAD="$here/payload"
    mkdir -p "$PAYLOAD"
    for part in server web; do
      if [[ ! -d "$PAYLOAD/$part" ]]; then
        echo "Downloading $part payload from release $RELEASE_TAG"
        curl -fsSL "https://github.com/$REPO/releases/download/$RELEASE_TAG/$part.tar.gz" -o "$PAYLOAD/$part.tar.gz"
        mkdir -p "$PAYLOAD/$part"
        tar -xzf "$PAYLOAD/$part.tar.gz" -C "$PAYLOAD/$part"
      fi
    done
  fi
  contexts=()
  if [[ -n "$PAYLOAD" ]]; then
    contexts+=(--build-context "server-dist=$PAYLOAD/server" --build-context "web-dist=$PAYLOAD/web")
  fi
  echo "Building $IMAGE"
  docker buildx build --load --target runtime -t "$IMAGE" "${build_args[@]}" "${contexts[@]}" "$here/server"
fi

if ! docker container inspect "$CONTAINER" >/dev/null 2>&1; then
  docker run -d --name "$CONTAINER" -p "$PORT:8096" "$IMAGE" >/dev/null
elif [[ "$(docker container inspect -f '{{.State.Running}}' "$CONTAINER")" != true ]]; then
  docker start "$CONTAINER" >/dev/null
fi

JELLYFIN_URL="http://localhost:$PORT" node "$here/src/bootstrap.mjs"
