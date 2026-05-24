# ros2-tui-launcher

An FTXUI-based terminal UI for launching, monitoring, and managing ROS 2 nodes —
launch profiles, live logs, topic inspection, node graph, parameter editing, and
system/process telemetry, all from one keyboard-driven screen.

| Distro | Ubuntu | Status |
| ------ | ------ | ------ |
| Jazzy  | Noble  | Released |
| Humble | Jammy  | Planned (procps API differs — not yet ported) |

## Install (Debian package)

Grab the latest `.deb` from the [releases page](https://github.com/franklinselva/ros2-tui-launcher/releases),
then install with `apt` so runtime dependencies resolve automatically:

```bash
VERSION=0.1.0
curl -fsSLO "https://github.com/franklinselva/ros2-tui-launcher/releases/download/v${VERSION}/ros-jazzy-ros2-tui-launcher_${VERSION}-0noble_amd64.deb"
sudo apt install "./ros-jazzy-ros2-tui-launcher_${VERSION}-0noble_amd64.deb"
```

That installs the wrapper at `/usr/bin/rtl`. No need to source any ROS overlay
first — the wrapper auto-sources `/opt/ros/jazzy/setup.bash`.

To uninstall:

```bash
sudo apt remove ros-jazzy-ros2-tui-launcher
```

## Usage

```bash
rtl --help

# Load every profile YAML in a directory
rtl --profiles /usr/share/ros2_tui_launcher/config/profiles

# Load one specific profile file
rtl --config my-profile.yaml
```

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
rtl --profiles src/ros2-tui-launcher/config/profiles
```

System packages required: `libproc2-dev`, `libyaml-cpp-dev`, `libspdlog-dev`,
plus a working ROS 2 Jazzy install. FTXUI and SPSCQueue are fetched at
configure time.

### Building the Debian package locally

The repository ships a Docker-based packager so you do not need bloom or
debhelper on the host:

```bash
just deb            # produces dist/jazzy/*.deb
just release        # builds + creates a GitHub release with the .deb
```

## License

MIT — see [`package.xml`](package.xml).
