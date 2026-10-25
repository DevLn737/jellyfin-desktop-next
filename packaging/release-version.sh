#!/usr/bin/env bash
# Prints the version a package is built with, as GitHub Actions outputs:
#   base=0.1.0             CMakeLists.txt's version (MSI versions are numeric)
#   version=0.1.0-beta.1   the full version (CMake's plus a tag's suffix)
#   suffix=-beta.1         the pre-release suffix ("" for none)
#   release=true|false     whether this is a release tag (v<version>)
# RELEASE_TAG is the tag being released (v0.1.0 or v0.1.0-beta.1).
# Release tags must carry CMakeLists.txt's version: v0.1.0 or v0.1.0-beta.1.
set -euo pipefail

base=$(sed -nE 's/^project\([A-Za-z]+ VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
test -n "$base"
suffix=""
release=false
if [[ "${RELEASE_TAG:-}" == v* ]]; then
  tag=${RELEASE_TAG#v}
  if [[ ! "$tag" =~ ^([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$ ]]; then
    echo "Tag v$tag is not v<major>.<minor>.<patch>[-<pre-release>]" >&2
    exit 1
  fi
  if [[ "${BASH_REMATCH[1]}" != "$base" ]]; then
    echo "Tag v$tag does not match the version in CMakeLists.txt ($base)" >&2
    exit 1
  fi
  suffix=${BASH_REMATCH[2]}
  release=true
fi
echo "base=$base"
echo "version=$base$suffix"
echo "suffix=$suffix"
echo "release=$release"
