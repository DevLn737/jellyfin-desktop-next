#!/usr/bin/env bash
# Builds the test segment provider against the assemblies of the server image
# into OUT (a plugin folder: the DLL and meta.json). Needs only Docker.
#
#   scripts/test-server/segments-plugin/build.sh IMAGE OUT
set -euo pipefail

IMAGE="$1"
OUT="$2"
DOCKER="${DOCKER:-docker}"
SDK="mcr.microsoft.com/dotnet/sdk:10.0"
SRC="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

container=$($DOCKER create "$IMAGE")
$DOCKER cp "$container:/jellyfin/." "$WORK/jellyfin" >/dev/null
$DOCKER rm "$container" >/dev/null
# The server's ABI version, which the plugin targets.
version=$(grep -oE '"MediaBrowser.Controller/[0-9.]+' "$WORK/jellyfin/jellyfin.deps.json" | head -1 | cut -d/ -f2)

cp "$SRC/JfdTestSegments.csproj" "$SRC/Plugin.cs" "$WORK/"
# As the calling user, so the build output can be cleaned up afterwards.
$DOCKER run --rm --user "$(id -u):$(id -g)" -v "$WORK:/src" -w /src -e HOME=/tmp -e DOTNET_CLI_HOME=/tmp \
  -e DOTNET_CLI_TELEMETRY_OPTOUT=1 -e DOTNET_NOLOGO=1 "$SDK" \
  dotnet build -c Release -o /src/out -p:JELLYFIN_DIR=/src/jellyfin >&2

mkdir -p "$OUT"
cp "$WORK/out/JfdTestSegments.dll" "$OUT/"
cat > "$OUT/meta.json" <<JSON
{
  "category": "General",
  "guid": "5c3b6c1e-7f0a-4d1e-9a51-0f2a6c4b9d17",
  "name": "JFD Test Segments",
  "overview": "Media segments from sidecar files, for the client's tests",
  "description": "",
  "owner": "jellyfin-desktop-next",
  "targetAbi": "${version}",
  "version": "1.0.0.0",
  "changelog": "",
  "timestamp": "2026-01-01T00:00:00.0000000Z",
  "status": "Active",
  "autoUpdate": false,
  "assemblies": ["JfdTestSegments.dll"]
}
JSON
