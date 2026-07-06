# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The section matching a pushed `vX.Y.Z` tag becomes the GitHub release body, so
keep each version's notes self-contained and user-facing.

## [Unreleased]

## [0.3.2] - 2026-07-06

### Added
- Parameters view can now edit array parameters (`int[]`, `double[]`, `bool[]`,
  `string[]`) directly — enter a comma-separated list (brackets optional).
- Parameter edits are validated against the node's declared integer/floating-point
  range before being sent, so out-of-range values are rejected up front.
- A `Setting…` indicator with a 5-second timeout is shown while a parameter set is
  in flight, so the edit box no longer hangs on an unresponsive node.

### Changed
- Parameters view no longer lists hidden/infrastructure nodes (e.g. the
  `/_ros2cli_daemon_*` CLI daemon), which previously sorted to the top and
  default-selected into a dead "Parameter service not available" panel.
- The parameter value column is wider and the edit field scrolls horizontally, so
  long values and strings remain fully editable.
- Non-editable parameter types (`byte[]`, unset) now report "Type not editable"
  instead of entering an edit that fails only on commit.
- `j`/`k` now navigate the node and parameter lists alongside the arrow keys.

### Fixed
- Pressing Tab while editing a parameter no longer switches the top-level tab and
  silently discards the in-progress value; input-capturing screens receive Tab
  first. Panel switching is documented as `←→` (Tab was never wired to it).

## [0.3.1] - 2026-07-02

### Added
- `rtl create` — the interactive profile-scaffolding screen is now a standalone
  top-level command instead of a tab in the main TUI (replaces `rtl config new`).

### Changed
- Removed the Create tab from the main TUI. Tab hotkeys are now case-sensitive
  uppercase, freeing lowercase keys for per-screen actions.
- Widened the Topics view's topic-name column so longer names are no longer
  truncated.
- Nodes view now lists lifecycle nodes first (then alphabetical) with a stable
  order across refreshes, so their state is easy to scan.

### Fixed
- Nodes view now shows the live lifecycle state (active/inactive/unconfigured/…)
  for lifecycle nodes; the query was previously never issued and the column was
  always blank. Also fixed the `get_state` service name for root-namespace nodes.
- Lifecycle detection no longer misflags a plain node as lifecycle when another
  node's name is a suffix of it (e.g. `talker` vs `lc_talker`).
- The lowercase `c` key now clears the Logs view instead of being swallowed by
  the Create tab's hotkey.
- Mouse-wheel and keyboard scrolling now work on the Nodes, Launch, and
  Parameters views, which previously rendered full lists without a viewport.

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

[Unreleased]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.3.2...HEAD
[0.3.2]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/franklinselva/ros2-tui-launcher/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/franklinselva/ros2-tui-launcher/releases/tag/v0.1.0
