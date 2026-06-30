# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The section matching a pushed `vX.Y.Z` tag becomes the GitHub release body, so
keep each version's notes self-contained and user-facing.

## [Unreleased]

## [0.3.0] - 2026-06-30

### Added
- Jetson/Tegra integrated-GPU monitoring via sysfs (GPU load + thermal zone),
  used automatically on boards exposing `/etc/nv_tegra_release`.
- arm64 and ROS 2 Humble (Ubuntu Jammy) `.deb` packaging.
- GitHub Actions build matrix: jazzy/humble × amd64/arm64, native per-arch
  (no QEMU), with `.deb` validation and an install smoke-test gating every build.
- `.deb` validation script asserting package name, architecture, version, the
  matching ROS runtime dependency, and the shipped binary + `rtl` wrapper.

### Changed
- System/process monitoring now reads `/proc` and `/sys` directly instead of
  linking `libproc2`, making the build distro-agnostic and removing the
  procps-ng dependency (this was the Humble/Jammy packaging blocker).
- Normalized the `yaml-cpp` CMake target across distros (0.7 plain target vs 0.8
  namespaced target) so the same build works on Jammy and Noble.
- Release publishing is guarded: a tag must be on `main` and ship both
  architectures with the correct ROS runtime dependency before it is released.

### Fixed
- Release asset glob pinned to the current package version so stale `.deb`s from
  prior versions are never uploaded.

## [0.2.0] - 2026-05-24

### Added
- Nested CLI subcommands (`rtl config …`) built on CLI11.
- ROS 2 graph profile generator: scaffold a launch profile from the live node
  and topic graph (`rtl config generate`).

## [0.1.0] - 2026-05-24

### Added
- Initial FTXUI-based terminal UI to launch, monitor, and manage ROS 2 nodes
  from YAML launch profiles.
- System monitoring (CPU, memory, per-process tree) with optional NVML GPU stats.
- Parameter screen for inspecting and editing ROS 2 node parameters.
- Mouse support and search across TUI screens.
- File-persistent logging and launch-profile validation.
- Docker-based `.deb` packaging and the `rtl` CLI wrapper.

[Unreleased]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/franklinselva/ros2-tui-launcher/releases/tag/v0.1.0
