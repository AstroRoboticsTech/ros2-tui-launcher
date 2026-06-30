#!/usr/bin/env bash
# Docker entrypoint for the .deb packager image. The image (packaging/Dockerfile)
# pre-installs the ROS + packaging toolchain; the actual build logic lives in
# scripts/build-deb-steps.sh so it can be shared verbatim with CI.
set -eo pipefail

export ROS_DISTRO="${ROS_DISTRO:?ROS_DISTRO must be set}"
export SRC_DIR="${SRC_DIR:-/src}"
export OUT_DIR="${OUT_DIR:-/out}"

exec "${SRC_DIR}/scripts/build-deb-steps.sh"
