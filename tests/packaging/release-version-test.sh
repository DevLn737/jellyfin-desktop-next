#!/usr/bin/env bash
# ctest release_version_check: packaging/release-version.sh maps tags to MSI
# versions that Windows Installer orders as SemVer orders the tags.
#   release-version-test.sh <repository root>
set -euo pipefail
script="$(cd "$1" && pwd)/packaging/release-version.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

# Prints the "msi=" output for CMake version $1 and tag $2 ("" for none).
msi() {
  printf 'project(JellyfinDesktopNext VERSION %s LANGUAGES CXX)\n' "$1" > "$work/CMakeLists.txt"
  (cd "$work" && RELEASE_TAG="$2" bash "$script") | sed -n 's/^msi=//p'
}

expect() { # base tag expected
  local got
  got=$(msi "$1" "$2")
  if [[ "$got" != "$3" ]]; then
    echo "FAIL: $1 tag '$2': msi=$got, expected $3"
    failures=$((failures + 1))
  fi
}

refuse() { # base tag
  printf 'project(JellyfinDesktopNext VERSION %s LANGUAGES CXX)\n' "$1" > "$work/CMakeLists.txt"
  if (cd "$work" && RELEASE_TAG="$2" bash "$script") > /dev/null 2>&1; then
    echo "FAIL: $1 tag '$2' was accepted"
    failures=$((failures + 1))
  fi
}

expect 0.0.1 "" 0.0.1000
expect 0.0.1 v0.0.1-alpha.2 0.0.1002
expect 0.0.1 v0.0.1-beta.1 0.0.1301
expect 0.0.1 v0.0.1-beta.9 0.0.1309
expect 0.0.1 v0.0.1-beta.10 0.0.1310
expect 0.0.1 v0.0.1-rc.1 0.0.1601
expect 0.0.1 v0.0.1 0.0.1999
expect 0.0.2 v0.0.2-beta.1 0.0.2301
expect 1.2.0 v1.2.0 1.2.999
expect 0.1.64 v0.1.64 0.1.64999

refuse 0.0.1 v0.0.1-beta
refuse 0.0.1 v0.0.1-beta.0
refuse 0.0.1 v0.0.1-beta.300
refuse 0.0.1 v0.0.1-preview.1
refuse 0.0.1 v0.0.1-beta.1.2
refuse 0.0.1 v0.0.2-beta.1
refuse 0.0.65 v0.0.65
refuse 256.0.0 v256.0.0

# Every version in SemVer order gets a strictly larger MSI build number.
ordered=("0.0.1 " "0.0.1 v0.0.1-alpha.1" "0.0.1 v0.0.1-alpha.299" "0.0.1 v0.0.1-beta.1" "0.0.1 v0.0.1-beta.8"
         "0.0.1 v0.0.1-beta.299" "0.0.1 v0.0.1-rc.1" "0.0.1 v0.0.1-rc.299" "0.0.1 v0.0.1"
         "0.0.2 " "0.0.2 v0.0.2-beta.1" "0.0.2 v0.0.2")
previous=-1
for entry in "${ordered[@]}"; do
  read -r base tag <<< "$entry"
  build=$(msi "$base" "${tag:-}" | cut -d. -f3)
  if (( build <= previous )); then
    echo "FAIL: '$entry' (build $build) does not sort after the version before it ($previous)"
    failures=$((failures + 1))
  fi
  previous=$build
done

if (( failures > 0 )); then
  exit 1
fi
echo "release-version.sh: MSI versions ok"
