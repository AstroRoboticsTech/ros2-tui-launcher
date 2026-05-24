#!/usr/bin/env bash
set -euo pipefail

ROS_DISTRO="${1:-jazzy}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${REPO_ROOT}/dist/${ROS_DISTRO}"
IMAGE="ros2-tui-launcher/packager:${ROS_DISTRO}"

case "${ROS_DISTRO}" in
  jazzy) ;;
  *) echo "Unsupported ROS_DISTRO: ${ROS_DISTRO} (only 'jazzy' is wired up today)" >&2; exit 2 ;;
esac

mkdir -p "${OUT_DIR}"

echo ">> Building image ${IMAGE}"
docker build \
  --build-arg ROS_DISTRO="${ROS_DISTRO}" \
  -t "${IMAGE}" \
  "${REPO_ROOT}/packaging"

echo ">> Running build container"
docker run --rm \
  -e ROS_DISTRO="${ROS_DISTRO}" \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -v "${REPO_ROOT}:/src:ro" \
  -v "${OUT_DIR}:/out" \
  "${IMAGE}"

echo ">> Artifacts:"
ls -la "${OUT_DIR}"
