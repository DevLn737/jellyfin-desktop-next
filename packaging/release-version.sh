#!/usr/bin/env bash
# Prints the version a package is built with, as GitHub Actions outputs:
#   base=0.1.0             CMakeLists.txt's version
#   version=0.1.0-beta.1   the full version (CMake's plus a tag's suffix)
#   suffix=-beta.1         the pre-release suffix ("" for none)
#   release=true|false     whether this is a release tag (v<version>)
#   msi=0.1.1301           the MSI ProductVersion (see below)
# RELEASE_TAG is the tag being released (v0.1.0 or v0.1.0-beta.1).
# Release tags must carry CMakeLists.txt's version: v0.1.0 or v0.1.0-beta.1.
#
# MSI ProductVersion. Windows Installer compares only the first three fields
# (major 0-255, minor 0-255, build 0-65535) when it decides whether a package
# is an upgrade or a downgrade; a fourth field is stored but ignored. So the
# pre-release has to live in the third field, ordered as SemVer orders it:
#   build = patch * 1000 + rank
#   rank    0          a build without a release tag (CI artifacts, local builds)
#           N          -alpha.N   (N = 1..299)
#           300 + N    -beta.N    (N = 1..299)
#           600 + N    -rc.N      (N = 1..299)
#           999        the release itself (no suffix)
# 0.0.1-beta.9 is 0.0.1309, 0.0.1-rc.1 is 0.0.1601, 0.0.1 is 0.0.1999 and
# 0.0.2-beta.1 is 0.0.2301. A newer pre-release upgrades an older one, and an
# older MSI run over a newer one stops with the downgrade message. patch can be
# at most 64 (64999 fits the field). Other pre-release names are refused so a
# tag can never map to a version that sorts wrongly. Betas up to beta.8 were
# all built as 0.0.1, which every version from this scheme upgrades.
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

IFS=. read -r major minor patch <<< "$base"
if (( major > 255 || minor > 255 || patch > 64 )); then
  echo "Version $base does not fit an MSI version (major and minor at most 255, patch at most 64)" >&2
  exit 1
fi
if [[ "$release" != true ]]; then
  rank=0
elif [[ -z "$suffix" ]]; then
  rank=999
elif [[ "$suffix" =~ ^-(alpha|beta|rc)\.([1-9][0-9]*)$ ]] && (( BASH_REMATCH[2] <= 299 )); then
  case "${BASH_REMATCH[1]}" in
    alpha) rank=${BASH_REMATCH[2]} ;;
    beta) rank=$((300 + BASH_REMATCH[2])) ;;
    rc) rank=$((600 + BASH_REMATCH[2])) ;;
  esac
else
  echo "Pre-release $suffix is not -alpha.N, -beta.N or -rc.N with N from 1 to 299 (needed for the MSI version)" >&2
  exit 1
fi

echo "base=$base"
echo "version=$base$suffix"
echo "suffix=$suffix"
echo "release=$release"
echo "msi=$major.$minor.$((patch * 1000 + rank))"
