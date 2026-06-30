#!/usr/bin/env bash
# Build the ros2_tui_launcher .deb from inside an already-prepared ROS
# environment (a ros:<distro>-ros-base container, or a CI job using that image).
#
# Assumes ROS is installed at /opt/ros/$ROS_DISTRO and the packaging toolchain
# (bloom, rosdep, fakeroot, debhelper) plus build deps are available. This is the
# single source of truth shared by the local Docker flow (packaging/entrypoint.sh)
# and the GitHub Actions matrix.
#
# Env:
#   ROS_DISTRO          required (e.g. jazzy)
#   SRC_DIR             source checkout (default: current dir)
#   OUT_DIR             where to drop the .deb (default: $SRC_DIR/dist/$ROS_DISTRO)
#   HOST_UID/HOST_GID   optional — chown outputs back to the host user
set -eo pipefail

ROS_DISTRO="${ROS_DISTRO:?ROS_DISTRO must be set}"
SRC_DIR="${SRC_DIR:-$PWD}"
OUT_DIR="${OUT_DIR:-${SRC_DIR}/dist/${ROS_DISTRO}}"

# Refresh the rosdep cache for the *current* HOME. The ros base image caches it
# under /root/.ros, but CI container jobs run with HOME=/github/home, so the cache
# must be (re)built here or bloom fails with "rosdep database has no sources".
rosdep update --rosdistro="${ROS_DISTRO}"

# Copy source into a writable workspace (the host source may be a read-only mount).
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

# Patch debian/rules to:
#   - allow CMake FetchContent (FTXUI, SPSCQueue, CLI11) to download during build;
#     bloom defaults FETCHCONTENT_FULLY_DISCONNECTED=ON, which breaks builds
#   - flip on the /usr/bin/rtl wrapper for the packaged build only.
sed -i 's|-DFETCHCONTENT_FULLY_DISCONNECTED=ON|-DFETCHCONTENT_FULLY_DISCONNECTED=OFF|g' \
  debian/rules || true
printf '\noverride_dh_auto_configure:\n\tdh_auto_configure -- -DFETCHCONTENT_FULLY_DISCONNECTED=OFF -DRTL_INSTALL_WRAPPER=ON\n' \
  >> debian/rules

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
