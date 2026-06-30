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

# Release notes: the CHANGELOG.md section for this version + install snippet.
# Same source of truth the CI publish step uses.
NOTES_FILE="$(mktemp)"
trap 'rm -f "${NOTES_FILE}"' EXIT
{
  "${REPO_ROOT}/scripts/changelog-extract.sh" "${VERSION}"
  printf '\n## Install\n\n```\nsudo apt install ./ros-%s-ros2-tui-launcher_%s-*.deb\nsource /opt/ros/%s/setup.bash\nrtl\n```\n' \
    "${ROS_DISTRO}" "${VERSION}" "${ROS_DISTRO}"
} > "${NOTES_FILE}"

if gh release view "${TAG}" >/dev/null 2>&1; then
  echo ">> Release ${TAG} exists — updating notes and uploading (clobber) assets"
  gh release edit "${TAG}" --notes-file "${NOTES_FILE}"
  gh release upload "${TAG}" "${debs[@]}" --clobber
else
  echo ">> Creating release ${TAG}"
  gh release create "${TAG}" "${debs[@]}" --title "${TAG}" --notes-file "${NOTES_FILE}"
fi

echo ">> Done. Release URL:"
gh release view "${TAG}" --json url -q .url
