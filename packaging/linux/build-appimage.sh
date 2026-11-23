#!/usr/bin/env bash
# Packs the built application into an AppImage with linuxdeploy and its Qt
# plugin, both pinned in deps/versions.json (linux.linuxdeploy*).
#
#   packaging/linux/build-appimage.sh EXECUTABLE QML_DIR LIBMPV_PREFIX OUTPUT_DIR
#
# qmake of the pinned Qt must be on PATH (or in QMAKE). libmpv and its FFmpeg
# come from LIBMPV_PREFIX (packaging/linux/build-libmpv.sh). Libraries that
# must match the running system stay out of the image: the graphics and VA-API
# drivers' libraries, audio servers' client libraries, GnuTLS (it knows the
# distribution's CA store), Wayland and libudev (SDL, linked into the
# executable for gamepads, loads the system's at run time).
set -euo pipefail

exe=$(realpath "${1:?usage: build-appimage.sh EXECUTABLE QML_DIR LIBMPV_PREFIX OUTPUT_DIR}")
qml=$(realpath "${2:?}")
mpv_prefix=$(realpath "${3:?}")
output=$(realpath -m "${4:?}")
root=$(cd "$(dirname "$0")/../.." && pwd)
pins="$root/deps/versions.json"
here="$root/packaging/linux"
app_id=io.github.devln737.JellyfinDesktopNext
name=$(basename "$exe")
# The icon and the AppImage file.
package=jellyfin-desktop-next

work="$output/work"
tools="$work/tools"
appdir="$work/AppDir"
rm -rf "$work"
mkdir -p "$tools" "$appdir/usr/bin" "$appdir/usr/share/metainfo"

fetch() {
  local key=$1 url sha bytes file
  url=$(jq -er ".linux.$key.url" "$pins")
  sha=$(jq -er ".linux.$key.sha256" "$pins")
  bytes=$(jq -er ".linux.$key.bytes" "$pins")
  file="$tools/$(jq -er ".linux.$key.name" "$pins")"
  curl -fsSL --retry 4 -o "$file" "$url"
  if [[ $(stat -c %s "$file") != "$bytes" ]] || ! echo "$sha  $file" | sha256sum -c --quiet -; then
    echo "$key: $(stat -c %s "$file") bytes, $(sha256sum "$file" | cut -d' ' -f1); pinned $bytes bytes, $sha" >&2
    exit 1
  fi
  chmod +x "$file"
}
fetch linuxdeploy
fetch linuxdeployPluginQt

install -m 755 "$exe" "$appdir/usr/bin/$name"
install -m 644 "$here/$app_id.metainfo.xml" "$appdir/usr/share/metainfo/$app_id.metainfo.xml"
# Licences of everything in the image (packaging/licenses/THIRD-PARTY-NOTICES.txt);
# the system libraries' copyright files are added after linuxdeploy below.
licenses="$appdir/usr/share/licenses/$package"
install -d "$licenses"
install -m 644 "$root/LICENSE" "$root/LICENSES.md" "$root"/packaging/licenses/*.txt "$licenses/"

excludes=()
for pattern in 'libva.so*' 'libva-*.so*' 'libvdpau*.so*' 'libdrm*.so*' 'libgbm.so*' \
               'libvulkan.so*' 'libEGL*.so*' 'libGL*.so*' 'libOpenGL.so*' \
               'libpipewire-*.so*' 'libpulse*.so*' 'libasound.so*' \
               'libgnutls.so*' 'libwayland-*.so*' 'libdbus-1.so*' 'libsystemd.so*' 'libudev.so*'; do
  excludes+=(--exclude-library "$pattern")
done

# The AppImage excludelist drops FriBiDi, but many systems (Arch without a GTK
# desktop, minimal KDE installs) do not have it; libass needs it, so it ships.
forced=()
for lib in libfribidi.so.0; do
  path=$(LD_LIBRARY_PATH="$mpv_prefix/lib" ldd "$mpv_prefix/lib/libass.so" | awk -v l="$lib" '$1 == l {print $3}')
  test -f "$path" || { echo "cannot find $lib" >&2; exit 1; }
  forced+=(--library "$path")
done

export APPIMAGE_EXTRACT_AND_RUN=1
# linuxdeploy finds its plugins on PATH.
export PATH="$tools:$PATH"
export LD_LIBRARY_PATH="$mpv_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QML_SOURCES_PATHS="$qml"
# Wayland is first-class on Linux (KDE Plasma and GNOME); X11 comes by default.
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export LDAI_OUTPUT="$output/$package-x86_64.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="${JFD_PACKAGE_VERSION:-dev}"
cd "$work"
# First the libraries and Qt's plugins, then (below, after their licences are
# in) the AppImage itself.
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
  --desktop-file "$here/$app_id.desktop" \
  --icon-file "$root/packaging/icons/$package.png" --icon-filename "$package" \
  "${excludes[@]}" "${forced[@]}" \
  --plugin qt

# Every library in the image has its licence: Qt's (and the ICU it is built
# with) and the pinned libmpv, FFmpeg, libplacebo and libass have theirs in
# packaging/licenses; any other comes from this system, and its package's
# Debian copyright file goes in. A library without either stops the build.
icu_major=73
install -d "$licenses/system"
{
  echo "Libraries of the build system in this AppImage, with the Ubuntu package"
  echo "each came from; that package's copyright file is <package>.copyright here."
  echo
} > "$licenses/system/README.txt"
unlicensed=()
while IFS= read -r lib; do
  base=$(basename "$lib")
  case "$base" in
    libQt6*) continue ;;
    libicu*)
      if [[ "$base" != *.so."$icu_major"* ]]; then
        echo "$base: Qt now ships another ICU than $icu_major; update packaging/licenses/ICU.txt and icu_major" >&2
        exit 1
      fi
      continue ;;
    libmpv.so*|libavcodec.so*|libavformat.so*|libavutil.so*|libavfilter.so*|libavdevice.so*| \
    libswscale.so*|libswresample.so*|libplacebo.so*|libass.so*)
      if [[ -e "$mpv_prefix/lib/$base" ]]; then continue; fi ;;
  esac
  owner=$(dpkg -S "*/$base" 2>/dev/null | grep -v '^diversion' | head -n 1 | cut -d: -f1 || true)
  copyright="/usr/share/doc/$owner/copyright"
  if [[ -z "$owner" || ! -r "$copyright" ]]; then
    unlicensed+=("$base")
    continue
  fi
  cp -L "$copyright" "$licenses/system/$owner.copyright"
  echo "$base: $owner $(dpkg-query -W -f='${Version}' "$owner")" >> "$licenses/system/README.txt"
done < <(find "$appdir/usr/lib" \( -type f -o -type l \) -name '*.so*' | sort)
if (( ${#unlicensed[@]} )); then
  echo "no licence for these libraries in the image: ${unlicensed[*]}" >&2
  echo "(add their texts to packaging/licenses and THIRD-PARTY-NOTICES.txt, or exclude them)" >&2
  exit 1
fi
test -f "$licenses/THIRD-PARTY-NOTICES.txt"

"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --output appimage

# A program that plays video must carry the pinned libmpv, not a system one.
if readelf -d "$exe" | grep -q 'NEEDED.*libmpv\.so'; then
  test -n "$(find "$appdir/usr/lib" -name 'libmpv.so*' -print -quit)"
fi
test -n "$(find "$appdir/usr/lib" -name 'libfribidi.so*' -print -quit)"
# Gamepads: the pinned SDL is inside the executable (static), never a system
# libSDL3, and libudev stays the system's.
if grep -q -a 'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS' "$exe"; then
  if readelf -d "$exe" | grep -q 'NEEDED.*libSDL3'; then
    echo "the executable needs a system libSDL3" >&2
    exit 1
  fi
  test -z "$(find "$appdir/usr/lib" -name 'libudev.so*' -print -quit)"
  test -f "$appdir/usr/share/licenses/$package/SDL3.txt"
fi
echo "AppImage: $LDAI_OUTPUT ($(du -h "$LDAI_OUTPUT" | cut -f1))"
