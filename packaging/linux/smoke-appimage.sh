#!/usr/bin/env bash
# Runs the AppImage's smoke test inside a distribution container: installs what
# a desktop system has (Mesa, X11, audio and VA-API client libraries), starts
# Xvfb and runs --smoke (plays a generated video and exits with 0).
#
#   packaging/linux/smoke-appimage.sh APPIMAGE        (inside the container)
set -euo pipefail

appimage=$(realpath "${1:?usage: smoke-appimage.sh APPIMAGE}")
. /etc/os-release

case "$ID" in
  ubuntu|debian)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends xvfb libgl1 libegl1 libopengl0 libgl1-mesa-dri libglx-mesa0 \
      libfontconfig1 libfreetype6 libx11-xcb1 libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
      libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
      libwayland-client0 libwayland-cursor0 libwayland-egl1 libdbus-1-3 \
      libva2 libva-drm2 libva-x11-2 libva-wayland2 libdrm2 libgbm1 libpulse0 libpipewire-0.3-0t64 libasound2t64 \
      libgnutls30t64 ca-certificates >/dev/null
    ;;
  fedora)
    dnf install -y -q xorg-x11-server-Xvfb mesa-dri-drivers mesa-libEGL mesa-libGL libglvnd-opengl mesa-libgbm \
      fontconfig freetype libxkbcommon libxkbcommon-x11 xcb-util-cursor xcb-util-wm xcb-util-image \
      xcb-util-keysyms xcb-util-renderutil libwayland-client libwayland-cursor libwayland-egl dbus-libs \
      libva libdrm pulseaudio-libs pipewire-libs alsa-lib gnutls ca-certificates >/dev/null
    ;;
  arch)
    pacman -Syu --noconfirm --needed -q xorg-server-xvfb mesa libglvnd fontconfig freetype2 libxkbcommon-x11 \
      xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil wayland dbus \
      libva libdrm libpulse pipewire alsa-lib gnutls ca-certificates >/dev/null
    ;;
  *)
    echo "unknown distribution $ID" >&2
    exit 1
    ;;
esac

Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp &
xvfb=$!
trap 'kill $xvfb 2>/dev/null || true' EXIT
for _ in $(seq 50); do [[ -e /tmp/.X11-unix/X99 ]] && break; sleep 0.1; done

# Every library the image needs from the system, in one list: the program and
# its bundled libraries must resolve; Qt plugins that miss one are only skipped
# by Qt, so those are reported but do not fail.
extract=$(mktemp -d)
(cd "$extract" && "$appimage" --appimage-extract >/dev/null)
root="$extract/squashfs-root"
missing() {
  find "$@" -type f \( -name '*.so*' -o -perm -u+x \) -print0 |
    xargs -0 -r env LD_LIBRARY_PATH="$root/usr/lib" ldd 2>/dev/null | awk '/not found/ {print $1}' | sort -u
}
required=$(missing "$root/usr/bin" "$root/usr/lib" -maxdepth 1)
optional=$(missing "$root/usr/plugins" "$root/usr/qml")
[[ -z "$optional" ]] || echo "Qt plugins or QML modules missing system libraries: $(echo $optional)"
if [[ -n "$required" ]]; then
  echo "missing system libraries: $(echo $required)" >&2
  exit 1
fi
rm -rf "$extract"

export DISPLAY=:99 APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=xcb
echo "$PRETTY_NAME, glibc $(ldd --version | head -1 | awk '{print $NF}')"
timeout 120 "$appimage" --smoke
echo "smoke test passed on $PRETTY_NAME"
