#!/usr/bin/env bash
set -euo pipefail

ROS_DISTRO="${1:-jazzy}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist/${ROS_DISTRO}"

if ! command -v gh >/dev/null; then
  echo "ERROR: gh CLI not found" >&2
  exit 1
fi

VERSION="$(grep -oP '(?<=<version>)[^<]+' "${REPO_ROOT}/package.xml")"
if [[ -z "${VERSION}" ]]; then
  echo "ERROR: could not parse <version> from package.xml" >&2
  exit 1
fi

TAG="v${VERSION}"
shopt -s nullglob
# Match any architecture (amd64, arm64, …) — collect every per-arch deb present.
debs=("${DIST_DIR}"/ros-"${ROS_DISTRO}"-ros2-tui-launcher_"${VERSION}"-*_*.deb)
if [[ ${#debs[@]} -eq 0 ]]; then
  echo "ERROR: no .deb matching version ${VERSION} in ${DIST_DIR}." >&2
  echo "       Run 'just deb ${ROS_DISTRO}' first." >&2
  exit 1
fi

echo ">> Uploading ${#debs[@]} asset(s):"
printf '   %s\n' "${debs[@]##*/}"

cd "${REPO_ROOT}"

if gh release view "${TAG}" >/dev/null 2>&1; then
  echo ">> Release ${TAG} exists — uploading (clobber) assets"
  gh release upload "${TAG}" "${debs[@]}" --clobber
else
  echo ">> Creating release ${TAG}"
  NOTES="Automated build of ${TAG} for ROS 2 ${ROS_DISTRO} (Ubuntu $(case ${ROS_DISTRO} in jazzy) echo noble;; humble) echo jammy;; *) echo unknown;; esac)).

Install:
\`\`\`
sudo apt install ./ros-${ROS_DISTRO}-ros2-tui-launcher_${VERSION}-*.deb
source /opt/ros/${ROS_DISTRO}/setup.bash
ros2 run ros2_tui_launcher ros2-tui-launcher
\`\`\`"
  gh release create "${TAG}" "${debs[@]}" --title "${TAG}" --notes "${NOTES}"
fi

echo ">> Done. Release URL:"
gh release view "${TAG}" --json url -q .url
