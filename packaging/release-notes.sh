#!/usr/bin/env bash
# Prints the notes of a pre-release (Markdown): what changed since the previous
# client release, how to install it, and that the MSI has no code-signing
# certificate (SmartScreen warns). package.yml publishes them with the files.
#   release-notes.sh <tag> [<commit>]
# Needs the history and the tags (actions/checkout with fetch-depth: 0).
# GITHUB_REPOSITORY, when set, adds a link to the full comparison.
set -euo pipefail
tag=$1
commit=${2:-HEAD}

# The newest client tag (v<version>) the commit contains, other than this one.
previous=$(git describe --tags --abbrev=0 --match 'v[0-9]*' --exclude "$tag" "$commit" 2>/dev/null || true)

echo "## What changed"
echo
if [[ -z "$previous" ]]; then
  echo "The first pre-release."
else
  changes=$(git log --no-merges --format='- %s' "$previous..$commit")
  if [[ -n "$changes" ]]; then
    echo "Since $previous:"
    echo
    echo "$changes"
  else
    echo "No changes since $previous: the same build again."
  fi
  if [[ -n "${GITHUB_REPOSITORY:-}" ]]; then
    echo
    echo "Full comparison: https://github.com/$GITHUB_REPOSITORY/compare/$previous...$tag"
  fi
fi
cat <<'TEXT'

## How to install

- **Linux:** download the `.AppImage`, make it executable (`chmod +x`) and run it. It updates itself when a newer pre-release is out.
- **Windows:** run the `.msi` installer. It installs for the current user, without administrator rights, and updates itself.
- **Windows SmartScreen:** the installer is not signed with a code-signing certificate, so Windows may show "Windows protected your PC". Choose **More info**, then **Run anyway**.

Both files are signed with minisign (`minisign.pub`), and their SHA-256 sums are in `SHA256SUMS.txt`: check a file before you run it if it came from anywhere else.
TEXT
