#!/usr/bin/env bash
# Starts the official Jellyfin 12.2 image (no SyncPlay rate extension) with the
# harness's generated test clip, then bootstraps it like the forked server.
#
# Environment: IMAGE (default: the 12.2 image the fork image is based on),
# CONTAINER, PORT (default 8096), MEDIA (host folder for the clip).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
harness="$(cd "$here/../syncplay-harness" && pwd -P)"
IMAGE="${IMAGE:-jellyfin/jellyfin:12.2-amd64.20261005-225228}"
CONTAINER="${CONTAINER:-jfd-syncplay-stock}"
PORT="${PORT:-8096}"
MEDIA="${MEDIA:-$here/out/stock-media}"

mkdir -p "$MEDIA"
if ! docker container inspect "$CONTAINER" >/dev/null 2>&1; then
  cp "$harness/server/make-media.sh" "$MEDIA/make-media.sh"
  docker run -d --name "$CONTAINER" -p "$PORT:8096" -v "$MEDIA:/media" "$IMAGE" >/dev/null
  if ! ls "$MEDIA"/movies/*/*.webm >/dev/null 2>&1; then
    echo "Generating the test clip"
    docker exec -e FFMPEG=/usr/lib/jellyfin-ffmpeg/ffmpeg "$CONTAINER" sh /media/make-media.sh /media
  fi
elif [[ "$(docker container inspect -f '{{.State.Running}}' "$CONTAINER")" != true ]]; then
  docker start "$CONTAINER" >/dev/null
fi

JELLYFIN_URL="http://localhost:$PORT" node "$harness/src/bootstrap.mjs"
