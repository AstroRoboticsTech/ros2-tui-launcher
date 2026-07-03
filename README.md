# ros2-tui-launcher

An FTXUI-based terminal UI for launching, monitoring, and managing ROS 2 nodes —
launch profiles, live logs, topic inspection, node graph, parameter editing, and
system/process telemetry, all from one keyboard-driven screen.

| Distro | Ubuntu | Status |
| ------ | ------ | ------ |
| Jazzy  | Noble  | Released |
| Humble | Jammy  | Released |

System/process monitoring reads `/proc` directly, so there is no `libproc2`/procps
version dependency — the same code runs on Noble and Jammy. Prebuilt `.deb`s are
published for both `amd64` and `arm64` (e.g. NVIDIA Jetson) on each distro.

## Install (Debian package)

Grab the latest `.deb` from the [releases page](https://github.com/franklinselva/ros2-tui-launcher/releases),
then install with `apt` so runtime dependencies resolve automatically:

```bash
VERSION=0.3.1
DISTRO=jazzy                                  # jazzy (Noble) or humble (Jammy)
ARCH=$(dpkg --print-architecture)             # amd64 or arm64
CODENAME=$(. /etc/os-release; echo "$UBUNTU_CODENAME")  # noble or jammy
DEB="ros-${DISTRO}-ros2-tui-launcher_${VERSION}-0${CODENAME}_${ARCH}.deb"
curl -fsSLO "https://github.com/franklinselva/ros2-tui-launcher/releases/download/v${VERSION}/${DEB}"
sudo apt install "./${DEB}"
```

That installs the wrapper at `/usr/bin/rtl`. No need to source any ROS overlay
first — the wrapper auto-sources `/opt/ros/${DISTRO}/setup.bash`.

To uninstall:

```bash
sudo apt remove ros-jazzy-ros2-tui-launcher
```

## Usage

```bash
rtl --help
rtl --version

# Launch the TUI (default subcommand)
rtl

# Load every profile YAML in a directory
rtl tui --profiles /usr/share/ros2_tui_launcher/config/profiles

# Load one specific profile file (long form)
rtl tui --config my-profile.yaml

# Shorthand: open this file directly in the TUI
rtl config my-profile.yaml
```

### Subcommands

```
rtl                                Launch the TUI with profiles from .
rtl tui [--profiles DIR | --config FILE]
                                   Explicit launch
rtl config <FILE.yaml>             Open FILE in the TUI
rtl config generate [opts]         Scaffold a profile from the running ROS 2 graph
rtl create                         Open the interactive Create screen
rtl config validate <FILE>         Schema-check a profile YAML
rtl config list [--profiles DIR]   List profiles found in a directory
```

### Generate profiles from a live ROS 2 graph

`rtl config generate` introspects the running ROS 2 daemon, observes published
topics for a few seconds, and emits a launch-profile YAML scaffold:

```bash
# Print to stdout
rtl config generate --sample-seconds 4

# Write to a file and open it in the TUI immediately
rtl config generate --output my-graph.yaml --launch
```

For an interactive flow with checkboxes per discovered node/topic and inline
package/executable editing, use the Create screen. It is a standalone command,
not a tab in the main TUI:

```bash
rtl create              # opens straight into the Create screen
```

The generator captures:

- one launch entry per discovered node. `package` and `executable` are
  best-effort inferred by scanning `/proc/*/cmdline` for ROS 2 install paths
  (e.g. `/opt/ros/<distro>/lib/<pkg>/<exe>` or `install/<pkg>/lib/<pkg>/<exe>`).
  Falls back to `<FILL>` placeholders when the node is remote, runs in a
  container with a different `/proc`, or was started via a non-matching path;
- one `monitored_topics` entry per active topic, with the measured publish
  rate pre-populated (sampled over 2–3 s).

Anything after `--ros-args` is forwarded verbatim to rclcpp (use this if you
need to override `ROS_DOMAIN_ID`, set parameter overrides, etc.).

Bundled example profiles ship under `/usr/share/ros2_tui_launcher/config/profiles/`
(talker/listener, lifecycle nodes, turtlesim, multi-node).

### Hotkeys

| Key | Screen |
| --- | ------ |
| `L` | Launch — start/stop/restart processes |
| `G` | Logs — aggregated stdout/stderr |
| `T` | Topics — live rate/bandwidth/type inspection |
| `N` | Nodes — graph view, publishers/subscribers/services |
| `P` | Parameters — list and edit node parameters |
| `Q` | Quit |

Tab hotkeys are case-sensitive uppercase (or use number keys `1`–`5`); lowercase
keys are reserved for per-screen actions (e.g. `c` clears the Logs view). Profile
scaffolding lives in the standalone `rtl create` command, not a TUI tab.

Mouse, arrow-key navigation, and incremental search work on every list view.

## Launch profile format

A profile is a YAML file describing one or more processes to manage. Minimal
example:

```yaml
profile: talker_listener
nodes:
  - name: talker
    package: demo_nodes_cpp
    executable: talker
    restart_policy: on-failure
  - name: listener
    package: demo_nodes_cpp
    executable: listener
    restart_policy: on-failure
```

Profiles support lifecycle nodes, ros-args remappings, parameter files,
environment overrides, and per-node restart policies. See the bundled examples
for the full schema in practice.

## Build from source

Standard colcon workflow inside a ROS 2 Jazzy workspace:

```bash
mkdir -p ~/rtl_ws/src
ln -s "$(pwd)" ~/rtl_ws/src/ros2-tui-launcher
cd ~/rtl_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select ros2_tui_launcher --symlink-install
source install/setup.bash
ros2 run ros2_tui_launcher ros2-tui-launcher tui --profiles src/ros2-tui-launcher/config/profiles
```

System packages required: `libyaml-cpp-dev`, `libspdlog-dev`, plus a working
ROS 2 (Jazzy or Humble) install. FTXUI, CLI11, and SPSCQueue are fetched at
configure time. Process/system monitoring reads `/proc` directly — no procps
dev package needed.

### Building the Debian package locally

The repository ships a Docker-based packager so you do not need bloom or
debhelper on the host:

```bash
just deb            # produces dist/jazzy/*.deb
just release        # builds + creates a GitHub release with the .deb
```

## License

MIT — see [`package.xml`](package.xml).
