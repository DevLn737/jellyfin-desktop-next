#!/usr/bin/env bash
# Timings for the "Startup", "Home and navigation" and "Library scroll"
# thresholds of stage 2a, using tests/ui/LibraryBench.cpp.
#
#   scripts/measure/library-bench.sh <path to jfd_library_bench> [warm runs]
#
# Needs JFD_TEST_SERVER_URL, JFD_TEST_USER and JFD_TEST_PASSWORD (the Docker
# test server) and a display (run under xvfb-run on Linux CI). Fails when
# startup or navigation is over its threshold; the scroll numbers are only
# reported, since the threshold is set for the Iris Xe laptop, not Xvfb.
# Appends a Markdown table to $GITHUB_STEP_SUMMARY when it is set.
set -euo pipefail

bench=$1
warm_runs=${2:-5}
profile=$(mktemp -d)
trap 'rm -rf "$profile"' EXIT

field() { jq -r ".$2 // empty" <<<"$1"; }
run() {
  local out
  out=$("$bench" "$@" 2>/dev/null | grep '^{' | tail -1)
  if [[ -z $out || -n $(field "$out" error) ]]; then
    echo "jfd_library_bench $*: ${out:-no output}" >&2
    exit 1
  fi
  echo "$out"
}
median() { sort -n | awk '{ v[NR] = $1 } END { print v[int((NR + 1) / 2)] }'; }

run sign-in "$profile" >/dev/null

# Cold: no image cache and, where allowed, no file system cache.
rm -rf "$profile/images"
sync
echo 3 | sudo -n tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || echo "note: file system cache not dropped" >&2
cold=$(run startup "$profile")
echo "cold:  $cold"

warm=()
for ((i = 0; i < warm_runs; ++i)); do
  warm+=("$(run startup "$profile")")
  echo "warm:  ${warm[-1]}"
done
warm_median() { for r in "${warm[@]}"; do field "$r" "$1"; done | median; }

scroll=$(run scroll)
echo "scroll: $scroll"

cold_ms=$(field "$cold" startupMs)
warm_ms=$(warm_median startupMs)
series_ms=$(warm_median seriesMs)
season_ms=$(warm_median seasonMs)
home_ms=$(warm_median homeMs)

failed=0
check() { # name value limit
  local verdict=ok
  if (($2 >= $3)); then verdict=OVER; failed=1; fi
  printf '| %s | %s ms | < %s ms | %s |\n' "$1" "$2" "$3" "$verdict"
}
table=$(
  echo "| Measurement | Value | Threshold | |"
  echo "|---|---|---|---|"
  check "Cold start to home" "$cold_ms" 4000
  check "Warm start to home (median of $warm_runs)" "$warm_ms" 2000
  check "Series page (warm cache, median)" "$series_ms" 1000
  check "Season page (warm cache, median)" "$season_ms" 1000
  check "Back to home (median)" "$home_ms" 1000
  printf '| 1000-item grid scroll, p95 frame interval (Xvfb, preliminary) | %.1f ms | ≤ %.1f ms on the laptop | %s frames, %s slow, max %.1f ms |\n' \
    "$(field "$scroll" p95Ms)" "$(jq -r '.periodMs * 1.25' <<<"$scroll")" \
    "$(field "$scroll" frames)" "$(field "$scroll" slowFrames)" "$(field "$scroll" maxMs)"
)
echo "$table"
if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
  { echo "### Library timings"; echo; echo "$table"; } >>"$GITHUB_STEP_SUMMARY"
fi
exit "$failed"
