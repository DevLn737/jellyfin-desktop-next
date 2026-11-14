#!/usr/bin/env bash
# ctest release_notes_check: packaging/release-notes.sh lists the commits since
# the previous client tag and carries the install and SmartScreen notes.
#   release-notes-test.sh <repository root>
set -euo pipefail
script="$(cd "$1" && pwd)/packaging/release-notes.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
git -C "$work" init -q -b main
commit() { git -C "$work" -c user.name=alice -c user.email=alice@example.invalid commit -q --allow-empty -m "$1"; }
commit "Before the first release"
git -C "$work" tag v0.0.1-beta.1
commit "Old change (#1)"
git -C "$work" tag v0.0.1-beta.2
# Not a client release: never the previous tag.
git -C "$work" tag syncplay-test-server
commit "New change (#2)"
commit "Another change (#3)"

has() { # notes text
  if ! grep -qF -- "$2" <<< "$1"; then
    echo "FAIL: missing '$2' in:"
    echo "$1"
    failures=$((failures + 1))
  fi
}
lacks() { # notes text
  if grep -qF -- "$2" <<< "$1"; then
    echo "FAIL: unexpected '$2' in:"
    echo "$1"
    failures=$((failures + 1))
  fi
}

notes=$(cd "$work" && GITHUB_REPOSITORY=o/r bash "$script" v0.0.1-beta.3)
has "$notes" "Since v0.0.1-beta.2:"
has "$notes" "- New change (#2)"
has "$notes" "- Another change (#3)"
lacks "$notes" "Old change"
has "$notes" "https://github.com/o/r/compare/v0.0.1-beta.2...v0.0.1-beta.3"
has "$notes" "SmartScreen"
has "$notes" "Run anyway"
has "$notes" "SHA256SUMS.txt"

# The tag being released may already exist on the commit (a pushed tag).
git -C "$work" tag v0.0.1-beta.3
notes=$(cd "$work" && bash "$script" v0.0.1-beta.3 HEAD)
has "$notes" "Since v0.0.1-beta.2:"
has "$notes" "- Another change (#3)"
lacks "$notes" "compare/"

# Two tags on one commit: nothing new.
notes=$(cd "$work" && bash "$script" v0.0.1-beta.4 v0.0.1-beta.3)
has "$notes" "No changes since v0.0.1-beta.3"

# The first release has no previous tag.
notes=$(cd "$work" && bash "$script" v0.0.1-beta.1 v0.0.1-beta.1)
has "$notes" "The first pre-release."
has "$notes" "SmartScreen"

if (( failures > 0 )); then
  exit 1
fi
echo "release-notes.sh: notes ok"
