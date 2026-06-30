#!/usr/bin/env bash
# Validate a freshly built .deb before it gets published. Pure static checks
# (dpkg-deb only — no ROS, no install), so it runs locally, in CI, and under act.
#
# Usage: scripts/validate-deb.sh [ROS_DISTRO] [ARCH]
#   ROS_DISTRO  default: jazzy
#   ARCH        default: dpkg --print-architecture (amd64 | arm64)
#
# Looks for dist/<distro>/ros-<distro>-ros2-tui-launcher_<version>-*_<arch>.deb,
# then asserts package name, version, architecture, the ros-<distro>-rclcpp runtime
# dep, and that the binary + rtl wrapper are present. Exits non-zero on any failure.
set -euo pipefail

ROS_DISTRO="${1:-jazzy}"
ARCH="${2:-$(dpkg --print-architecture)}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist/${ROS_DISTRO}"

VERSION="$(grep -oP '(?<=<version>)[^<]+' "${REPO_ROOT}/package.xml")"
PKG="ros-${ROS_DISTRO}-ros2-tui-launcher"

shopt -s nullglob
debs=("${DIST_DIR}"/"${PKG}"_"${VERSION}"-*_"${ARCH}".deb)
if [[ ${#debs[@]} -eq 0 ]]; then
  echo "ERROR: no ${ARCH} .deb for version ${VERSION} in ${DIST_DIR}" >&2
  exit 1
fi
DEB="${debs[-1]}"
echo ">> Validating $(basename "${DEB}")"

# Snapshot the metadata once.
DEB_PACKAGE="$(dpkg-deb --field "${DEB}" Package)"
DEB_ARCH="$(dpkg-deb --field "${DEB}" Architecture)"
DEB_VERSION="$(dpkg-deb --field "${DEB}" Version)"
DEB_DEPENDS="$(dpkg-deb --field "${DEB}" Depends)"
DEB_CONTENTS="$(dpkg-deb --contents "${DEB}")"

fail=0
ok() {   # ok <description> ; reads $1 already evaluated truthiness via caller
  echo "  PASS: $1"
}
bad() {
  echo "  FAIL: $1" >&2
  fail=1
}

[[ "${DEB_PACKAGE}" == "${PKG}" ]]            && ok "package name is ${PKG}"            || bad "package name is ${PKG} (got ${DEB_PACKAGE})"
[[ "${DEB_ARCH}" == "${ARCH}" ]]              && ok "architecture is ${ARCH}"           || bad "architecture is ${ARCH} (got ${DEB_ARCH})"
[[ "${DEB_VERSION}" == ${VERSION}-* ]]        && ok "version is ${VERSION}"             || bad "version is ${VERSION} (got ${DEB_VERSION})"
grep -q "ros-${ROS_DISTRO}-rclcpp" <<<"${DEB_DEPENDS}" && ok "depends on ros-${ROS_DISTRO}-rclcpp" || bad "depends on ros-${ROS_DISTRO}-rclcpp (got: ${DEB_DEPENDS})"
grep -q 'lib/ros2_tui_launcher/ros2-tui-launcher' <<<"${DEB_CONTENTS}" \
                                              && ok "ships the ros2-tui-launcher binary" || bad "ships the ros2-tui-launcher binary"
grep -qE 'usr/bin/rtl$' <<<"${DEB_CONTENTS}"  && ok "ships the /usr/bin/rtl wrapper"    || bad "ships the /usr/bin/rtl wrapper"

echo ">> Package metadata:"
printf '   Package: %s\n   Version: %s\n   Architecture: %s\n   Depends: %s\n' \
  "${DEB_PACKAGE}" "${DEB_VERSION}" "${DEB_ARCH}" "${DEB_DEPENDS}"

if [[ ${fail} -ne 0 ]]; then
  echo ">> VALIDATION FAILED" >&2
  exit 1
fi
echo ">> All checks passed."
