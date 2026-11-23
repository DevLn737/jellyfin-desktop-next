#!/usr/bin/env bash
# A second start while the app runs hands over and exits without touching the
# running copy's log or settings (no rotation, no truncation, no new settings
# file). Usage: second-start-test.sh <jellyfin-desktop executable>
set -euo pipefail

app="$1"
root="$(mktemp -d)"
primary=""
# The first start quits on SIGTERM; it gets a few seconds, then SIGKILL.
stop_primary() {
  kill "$primary" 2>/dev/null || return 0
  (sleep 5; kill -9 "$primary" 2>/dev/null) > /dev/null 2>&1 &
  local watchdog=$!
  wait "$primary" 2>/dev/null || true
  kill "$watchdog" 2>/dev/null || true
}
cleanup() {
  if [ -n "$primary" ]; then stop_primary; fi
  rm -rf "$root"
}
trap cleanup EXIT

# A profile and instance of its own: the single-instance key is the
# application name and the user, its socket lives in TMPDIR.
export HOME="$root/home" TMPDIR="$root/tmp" XDG_RUNTIME_DIR="$root/run"
export XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data" XDG_CACHE_HOME="$root/cache"
export USER="jfd-second-start-$$" USERNAME="jfd-second-start-$$"
mkdir -p "$HOME" "$TMPDIR" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"

# No update check against GitHub from a test (a QSettings INI file).
printf '[integrations]\ncheckForUpdates=false\n' > "$root/first.ini"
"$app" --settings-file "$root/first.ini" > "$root/first.out" 2>&1 &
primary=$!

log=""
for _ in $(seq 1 300); do
  log="$(find "$XDG_DATA_HOME" -name jellyfin-desktop.log 2>/dev/null | head -1)"
  # Listening starts before the log, so a log means the instance is primary.
  if [ -n "$log" ] && [ -s "$log" ]; then break; fi
  if ! kill -0 "$primary" 2>/dev/null; then
    echo "The first start exited early:"; cat "$root/first.out"; exit 1
  fi
  sleep 0.1
done
[ -n "$log" ] && [ -s "$log" ] || { echo "The first start wrote no log"; cat "$root/first.out"; exit 1; }
logs="$(dirname "$log")"
inode="$(stat -c %i "$log")"
size="$(stat -c %s "$log")"
head="$(head -c "$size" "$log" | sha256sum)"

# The second start must hand over and exit 0 quickly.
if ! timeout 30 "$app" --settings-file "$root/second.ini" > "$root/second.out" 2>&1; then
  echo "The second start did not exit cleanly:"; cat "$root/second.out"; exit 1
fi

fail=0
if [ -e "$logs/jellyfin-desktop.old.log" ]; then echo "The second start rotated the running copy's log"; fail=1; fi
if [ "$(stat -c %i "$log" 2>/dev/null)" != "$inode" ]; then echo "The log file was replaced"; fail=1; fi
if [ "$(stat -c %s "$log")" -lt "$size" ] || [ "$(head -c "$size" "$log" | sha256sum)" != "$head" ]; then
  echo "The running copy's log was truncated or rewritten"; fail=1
fi
if [ -e "$root/second.ini" ]; then echo "The second start created its settings file"; fail=1; fi
if ! kill -0 "$primary" 2>/dev/null; then echo "The first start is gone"; fail=1; fi
[ "$fail" = 0 ] || { echo "--- second start:"; cat "$root/second.out"; exit 1; }
echo "second start handed over; the running copy's log is intact"
