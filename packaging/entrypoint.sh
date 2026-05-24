#!/usr/bin/env bash
set -eo pipefail

ROS_DISTRO="${ROS_DISTRO:?ROS_DISTRO must be set}"
OUT_DIR="${OUT_DIR:-/out}"
SRC_DIR="${SRC_DIR:-/src}"

# Copy source into writable workspace (host source is read-only mount)
WORK=/tmp/pkg
rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$SRC_DIR"/. "$WORK"/
cd "$WORK"

# Wipe any stale debian/ from prior runs
rm -rf debian obj-*

# shellcheck disable=SC1090
source "/opt/ros/${ROS_DISTRO}/setup.bash"

bloom-generate rosdebian \
  --os-name ubuntu \
  --os-version "$(lsb_release -cs)" \
  --ros-distro "${ROS_DISTRO}"

# Allow CMake FetchContent (FTXUI, SPSCQueue) to download during build.
# bloom defaults to FETCHCONTENT_FULLY_DISCONNECTED=ON, which breaks the build.
sed -i 's|-DFETCHCONTENT_FULLY_DISCONNECTED=ON|-DFETCHCONTENT_FULLY_DISCONNECTED=OFF|g' \
  debian/rules || true
# If the flag is not present, inject it into the dh_auto_configure override.
if ! grep -q 'FETCHCONTENT_FULLY_DISCONNECTED' debian/rules; then
  printf '\noverride_dh_auto_configure:\n\tdh_auto_configure -- -DFETCHCONTENT_FULLY_DISCONNECTED=OFF\n' >> debian/rules
fi

fakeroot debian/rules binary

mkdir -p "$OUT_DIR"
shopt -s nullglob
debs=(../*.deb)
if [[ ${#debs[@]} -eq 0 ]]; then
  echo "ERROR: no .deb produced" >&2
  exit 1
fi
cp -v "${debs[@]}" "$OUT_DIR"/

# Hand ownership back to host UID/GID if provided
if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
  chown -R "${HOST_UID}:${HOST_GID}" "$OUT_DIR"
fi
