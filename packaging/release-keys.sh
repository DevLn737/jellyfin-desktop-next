#!/usr/bin/env bash
# Prints the minisign public keys the client is built with, as lines for
# $GITHUB_ENV:
#   RELEASE_PUBLIC_KEY=RW...        packaging/release-keys/current.pub, the key
#                                   releases are signed with now
#   RELEASE_NEXT_PUBLIC_KEY=RW...   packaging/release-keys/next.pub, only during
#                                   a key rotation (empty when the file is absent)
# A key is the second line of minisign's .pub file. See "Release signing" in
# packaging/README.md.
set -euo pipefail
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/release-keys"

read_key() {
  local file="$1" key
  key="$(sed -n 2p "$file" | tr -d '\r')"
  if ! [[ "$key" =~ ^RW[A-Za-z0-9+/]+$ ]]; then
    echo "::error::$file is not a minisign public key file" >&2
    exit 1
  fi
  printf '%s' "$key"
}

current="$(read_key "$dir/current.pub")"
next=""
if [ -f "$dir/next.pub" ]; then
  next="$(read_key "$dir/next.pub")"
  if [ "$next" = "$current" ]; then
    echo "::error::next.pub is the same key as current.pub" >&2
    exit 1
  fi
fi
echo "RELEASE_PUBLIC_KEY=$current"
echo "RELEASE_NEXT_PUBLIC_KEY=$next"
