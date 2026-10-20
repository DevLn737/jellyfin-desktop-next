#!/usr/bin/env bash
# Builds the libmpv the AppImage bundles: mpv at the commit pinned for Windows,
# with FFmpeg, libplacebo, libass and the NVIDIA codec headers pinned in
# deps/versions.json (linux.sources). Everything is installed into PREFIX.
#
#   packaging/linux/build-libmpv.sh PREFIX [WORKDIR]
#
# Needs the build dependencies listed in packaging/linux/README.md (CI installs
# them). VA-API, PipeWire, PulseAudio, ALSA, GnuTLS and the Mesa libraries stay
# the system's: they are linked here and excluded from the AppImage.
set -euo pipefail

prefix=$(realpath -m "${1:?usage: build-libmpv.sh PREFIX [WORKDIR]}")
work=$(realpath -m "${2:-build/libmpv-src}")
root=$(cd "$(dirname "$0")/../.." && pwd)
pins="$root/deps/versions.json"
jobs=$(nproc)

pin() { jq -er ".linux.sources.$1.$2" "$pins"; }

# Shallow clone of a tag, verified against the object ls-remote reports for it
# (the tag object for annotated tags, the commit for lightweight ones).
fetch_tag() {
  local name=$1 dir="$work/$1"
  local repo tag object
  repo=$(pin "$name" repository)
  tag=$(pin "$name" tag)
  object=$(pin "$name" object)
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" fetch -q --depth 1 "$repo" "refs/tags/$tag:refs/tags/$tag"
  local actual
  actual=$(git -C "$dir" rev-parse "refs/tags/$tag")
  if [[ "$actual" != "$object" ]]; then
    echo "$name $tag is $actual, pinned $object" >&2
    exit 1
  fi
  git -C "$dir" -c advice.detachedHead=false checkout -q "refs/tags/$tag"
}

fetch_commit() {
  local name=$1 dir="$work/$1"
  local repo commit
  repo=$(pin "$name" repository)
  commit=$(pin "$name" commit)
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" fetch -q --depth 1 "$repo" "$commit"
  git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
  [[ $(git -C "$dir" rev-parse HEAD) == "$commit" ]]
}

mkdir -p "$work" "$prefix"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
meson_common=(--prefix "$prefix" --libdir lib --buildtype release -Ddefault_library=shared)

echo "::group::nv-codec-headers"
fetch_tag nvCodecHeaders
make -C "$work/nvCodecHeaders" PREFIX="$prefix" install
echo "::endgroup::"

echo "::group::FFmpeg"
fetch_tag ffmpeg
(
  cd "$work/ffmpeg"
  # LGPL build. Decoders, demuxers, filters (lavfi test sources) and network
  # protocols stay at their defaults; hardware decoding through VA-API and
  # NVDEC/CUVID (loaded at run time, no CUDA SDK needed).
  ./configure --prefix="$prefix" --libdir="$prefix/lib" \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
    --enable-gnutls --enable-libdav1d --enable-vaapi --enable-libdrm \
    --enable-ffnvcodec --enable-nvdec --enable-cuvid \
    --disable-vdpau --disable-libxcb --disable-xlib --disable-sdl2
  make -j"$jobs"
  make install
)
echo "::endgroup::"

echo "::group::libplacebo"
fetch_tag libplacebo
git -C "$work/libplacebo" submodule update -q --init --depth 1
# OpenGL only: the Linux client renders with OpenGL (ADR 0001).
meson setup "$work/libplacebo/build" "$work/libplacebo" "${meson_common[@]}" \
  -Dvulkan=disabled -Dopengl=enabled -Dd3d11=disabled -Dshaderc=disabled -Dglslang=disabled \
  -Dlcms=enabled -Ddemos=false -Dtests=false
meson install -C "$work/libplacebo/build"
echo "::endgroup::"

echo "::group::libass"
fetch_tag libass
meson setup "$work/libass/build" "$work/libass" "${meson_common[@]}" \
  -Dfontconfig=enabled -Dtest=disabled -Dprofile=disabled -Dcheckasm=disabled
meson install -C "$work/libass/build"
echo "::endgroup::"

echo "::group::mpv"
fetch_commit mpv
meson setup "$work/mpv/build" "$work/mpv" "${meson_common[@]}" \
  -Dlibmpv=true -Dcplayer=false -Dtests=false \
  -Dlua=luajit -Djavascript=disabled -Dvulkan=disabled \
  -Dgl=enabled -Degl=enabled -Dvaapi=enabled -Dpipewire=enabled -Dpulse=enabled -Dalsa=enabled \
  -Dmanpage-build=disabled -Dhtml-build=disabled -Dpdf-build=disabled
meson install -C "$work/mpv/build"
echo "::endgroup::"

pkg-config --modversion mpv
echo "mpv $(pin mpv commit) installed into $prefix"
