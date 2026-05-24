#include "screens/create_screen.hpp"

#include "ros2_tui_launcher/host_process_lookup.hpp"
#include "ros2_tui_launcher/profile_generator.hpp"
#include "ros2_tui_launcher/topic_monitor.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace ftxui;

namespace rtl::tui {

namespace {
const std::unordered_set<std::string> kExcludedNodePrefixes = {
    "_ros2cli_daemon",
    "ros2_tui_launcher",
    "ros2_tui_launcher_config_generate",
};

const std::unordered_set<std::string> kExcludedTopics = {
    "/rosout",
    "/parameter_events",
};

bool startsWithExcluded(const std::string& s) {
    for (const auto& p : kExcludedNodePrefixes) {
        if (s.rfind(p, 0) == 0) return true;
    }
    return false;
}

CheckboxOption makeAsciiCheckbox() {
    CheckboxOption opt;
    opt.transform = [](const EntryState& s) {
        auto box = text(s.state ? "[x] " : "[ ] ");
        auto lbl = text(s.label);
        auto row = hbox({box, lbl});
        return s.focused ? row | inverted : row;
    };
    return opt;
}

}  // namespace

CreateScreen::CreateScreen(rclcpp::Node::SharedPtr node) : node_(std::move(node)) {}

void CreateScreen::refreshSnapshot() {
    status_ = "Scanning ROS 2 graph (2s settle + 3s hz sample)...";

    GraphInspector inspector(node_);
    auto snap = inspector.snapshot(std::chrono::milliseconds(2000));

    std::vector<GraphTopic> kept_topics;
    for (const auto& gt : snap.topics) {
        if (kExcludedTopics.count(gt.name)) continue;
        if (gt.publisher_nodes.empty()) continue;
        kept_topics.push_back(gt);
    }

    std::unordered_map<std::string, double> measured_hz;
    if (!kept_topics.empty()) {
        TopicMonitor monitor(node_, std::chrono::milliseconds(200));
        std::vector<std::pair<std::string, double>> watched;
        watched.reserve(kept_topics.size());
        for (const auto& gt : kept_topics) {
            watched.emplace_back(gt.name, 0.0);
        }
        monitor.setWatchedTopics(watched);
        monitor.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        for (const auto& ti : monitor.snapshot()) {
            measured_hz[ti.name] = ti.hz;
        }
        monitor.stop();
    }

    auto procs = scanRos2HostProcesses();

    std::lock_guard lock(mutex_);
    snap_ = std::move(snap);

    nodes_.clear();
    int guessed = 0;
    for (const auto& gn : snap_.nodes) {
        if (startsWithExcluded(gn.name)) continue;
        NodeRow row;
        row.source = gn;
        row.label = (gn.ns.empty() || gn.ns == "/") ? gn.name : gn.ns + "/" + gn.name;

        const auto* p = matchHostProcess(gn.name, row.label, procs);
        if (p) {
            if (!p->package.empty())    row.package = p->package;
            if (!p->executable.empty()) row.executable = p->executable;
            if (!p->package.empty() && !p->executable.empty()) ++guessed;
        }

        nodes_.push_back(std::move(row));
    }

    topics_.clear();
    for (const auto& gt : kept_topics) {
        TopicRow row;
        row.source = gt;
        row.label = gt.name;
        row.type = gt.types.empty() ? "?" : gt.types.front();
        auto it = measured_hz.find(gt.name);
        row.hz = (it != measured_hz.end()) ? it->second : 0.0;
        topics_.push_back(std::move(row));
    }

    rebuildRows();

    status_ = "Found " + std::to_string(nodes_.size()) + " nodes ("
              + std::to_string(guessed) + " auto-filled), "
              + std::to_string(topics_.size()) + " topics.  "
              + "[r] rescan  [g] generate";
}

void CreateScreen::rebuildRows() {
    if (node_container_) node_container_->DetachAllChildren();
    if (topic_container_) topic_container_->DetachAllChildren();

    auto cb_opt = makeAsciiCheckbox();
    for (auto& row : nodes_) {
        row.checkbox = Checkbox(&row.label, &row.selected, cb_opt);
        row.package_input = Input(&row.package, "package");
        row.executable_input = Input(&row.executable, "executable");
        row.line = Container::Horizontal({
            row.checkbox, row.package_input, row.executable_input,
        });
        if (node_container_) node_container_->Add(row.line);
    }

    for (auto& row : topics_) {
        row.checkbox = Checkbox(&row.label, &row.selected, cb_opt);
        if (topic_container_) topic_container_->Add(row.checkbox);
    }
}

void CreateScreen::generate() {
    std::lock_guard lock(mutex_);

    LaunchProfile profile;
    profile.name = profile_name_.empty() ? std::string("generated") : profile_name_;
    profile.description = "Built interactively from a live ROS 2 graph.";

    for (const auto& row : nodes_) {
        if (!row.selected) continue;
        LaunchEntry e;
        e.name = row.label;
        e.package = row.package.empty() ? std::string("<FILL>") : row.package;
        e.executable = row.executable.empty() ? std::string("<FILL>") : row.executable;
        e.autostart = false;
        e.restart_policy = "on-failure";
        profile.entries.push_back(std::move(e));
    }

    for (const auto& row : topics_) {
        if (!row.selected) continue;
        MonitoredTopic mt;
        mt.topic = row.label;
        mt.expected_hz = row.hz;
        profile.monitored_topics.push_back(std::move(mt));
    }

    if (output_path_.empty()) {
        status_ = "ERROR: output path is empty";
        return;
    }

    std::ofstream f(output_path_);
    if (!f) {
        status_ = "ERROR: cannot open '" + output_path_ + "' for writing";
        return;
    }
    writeProfileYaml(profile, f);

    status_ = "Wrote " + output_path_ + " ("
              + std::to_string(profile.entries.size()) + " entries, "
              + std::to_string(profile.monitored_topics.size()) + " topics)";
}

Component CreateScreen::component() {
    if (cached_component_) return cached_component_;

    name_input_ = Input(&profile_name_, "profile name");
    path_input_ = Input(&output_path_, "./generated.yaml");
    node_container_ = Container::Vertical({});
    topic_container_ = Container::Vertical({});
    refresh_btn_ = Button("Refresh", [this] { refreshSnapshot(); });
    generate_btn_ = Button("Generate", [this] { generate(); });

    auto container = Container::Vertical({
        name_input_,
        path_input_,
        node_container_,
        topic_container_,
        refresh_btn_,
        generate_btn_,
    });

    constexpr int kNodeCol = 28;
    constexpr int kPkgCol = 22;
    constexpr int kExeCol = 22;
    constexpr int kTopicCol = 30;
    constexpr int kTypeCol = 26;
    constexpr int kHzCol = 9;

    auto renderer = Renderer(container, [this] {
        auto fmtHz = [](double hz) {
            if (hz <= 0.0 || !std::isfinite(hz)) return std::string("--");
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(hz < 10 ? 2 : 1) << hz;
            return ss.str();
        };
        if (!initial_refresh_done_) {
            initial_refresh_done_ = true;
            refreshSnapshot();
        }

        std::lock_guard lock(mutex_);

        auto node_header = hbox({
            text("node") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kNodeCol + 4),
            text("package") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kPkgCol),
            text("executable") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kExeCol),
        });

        Elements node_rows;
        node_rows.push_back(node_header);
        node_rows.push_back(separator());
        if (nodes_.empty()) {
            node_rows.push_back(
                text(" (none discovered — press 'r' to rescan)") | color(Color::Yellow));
        } else {
            for (auto& row : nodes_) {
                if (!row.checkbox) continue;
                bool needs_fill = (row.package == "<FILL>" || row.executable == "<FILL>");
                auto pkg_color = (row.package == "<FILL>") ? Color::Yellow : Color::Green;
                auto exe_color = (row.executable == "<FILL>") ? Color::Yellow : Color::Green;
                node_rows.push_back(hbox({
                    row.checkbox->Render() | size(WIDTH, EQUAL, kNodeCol + 4)
                        | (row.selected ? color(Color::White) : color(Color::GrayDark)),
                    row.package_input->Render() | size(WIDTH, EQUAL, kPkgCol)
                        | color(pkg_color),
                    row.executable_input->Render() | size(WIDTH, EQUAL, kExeCol)
                        | color(exe_color),
                }) | (needs_fill ? nothing : dim));
            }
        }
        auto nodes_box = window(
            text(" Discovered nodes (" + std::to_string(nodes_.size()) + ") ")
                | bold | color(Color::Cyan),
            vbox(node_rows) | vscroll_indicator | frame);

        auto topic_header = hbox({
            text("topic") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kTopicCol + 5),
            text("type") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kTypeCol),
            text("Hz") | bold | color(Color::Cyan) | size(WIDTH, EQUAL, kHzCol),
        });

        Elements topic_rows;
        topic_rows.push_back(topic_header);
        topic_rows.push_back(separator());
        if (topics_.empty()) {
            topic_rows.push_back(text(" (none discovered)") | color(Color::Yellow));
        } else {
            for (auto& row : topics_) {
                if (!row.checkbox) continue;
                auto hz_color = (row.hz > 0.0) ? Color::Green : Color::GrayDark;
                topic_rows.push_back(hbox({
                    row.checkbox->Render() | size(WIDTH, EQUAL, kTopicCol + 5)
                        | (row.selected ? color(Color::White) : color(Color::GrayDark)),
                    text(row.type) | size(WIDTH, EQUAL, kTypeCol) | color(Color::GrayLight),
                    text(fmtHz(row.hz)) | size(WIDTH, EQUAL, kHzCol) | color(hz_color),
                }));
            }
        }
        auto topics_box = window(
            text(" Monitored topics (" + std::to_string(topics_.size()) + ") ")
                | bold | color(Color::Cyan),
            vbox(topic_rows) | vscroll_indicator | frame);

        auto form = window(
            text(" Profile ") | bold | color(Color::Cyan),
            vbox({
                hbox({
                    text(" name   : ") | color(Color::GrayLight),
                    name_input_->Render() | color(Color::White) | flex,
                }),
                hbox({
                    text(" output : ") | color(Color::GrayLight),
                    path_input_->Render() | color(Color::White) | flex,
                }),
            }));

        auto btn_row = hbox({
            refresh_btn_->Render() | color(Color::Cyan),
            text("  "),
            generate_btn_->Render() | color(Color::Green),
            text("  "),
            text("[r] rescan  [g] generate  [Tab] move focus") | color(Color::GrayDark),
        });

        Color status_color = Color::GrayLight;
        if (status_.rfind("ERROR", 0) == 0) status_color = Color::Red;
        else if (status_.rfind("Wrote", 0) == 0) status_color = Color::Green;
        else if (status_.rfind("Scanning", 0) == 0) status_color = Color::Yellow;
        else if (status_.rfind("Found", 0) == 0) status_color = Color::Green;

        auto status_line = window(
            text(" Status ") | bold | color(Color::Cyan),
            text(status_) | color(status_color));

        auto body = hbox({
            nodes_box | flex_grow,
            topics_box | size(WIDTH, GREATER_THAN, kTopicCol + kTypeCol + kHzCol + 10),
        });

        return vbox({
            form,
            body | flex,
            btn_row,
            status_line,
        });
    });

    cached_component_ = CatchEvent(renderer, [this](Event ev) {
        if (ev == Event::Character('r') || ev == Event::Character('R')) {
            refreshSnapshot();
            return true;
        }
        if (ev == Event::Character('g') || ev == Event::Character('G')) {
            generate();
            return true;
        }
        return false;
    });

    return cached_component_;
}

}  // namespace rtl::tui
