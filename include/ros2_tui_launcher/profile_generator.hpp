#pragma once

#include "ros2_tui_launcher/graph_inspector.hpp"
#include "ros2_tui_launcher/launch_profile.hpp"
#include "ros2_tui_launcher/topic_monitor.hpp"

#include <chrono>
#include <iosfwd>
#include <string>
#include <unordered_set>

namespace rtl {

struct GenerateOptions {
    std::string profile_name;
    std::chrono::milliseconds hz_sample_window = std::chrono::milliseconds(2000);
    bool include_topics = true;
    bool include_nodes = true;
    bool host_lookup = true;

    std::unordered_set<std::string> exclude_node_names = {
        "_ros2cli_daemon",
        "ros2_tui_launcher",
        "ros2_tui_launcher_config_generate",
    };
    std::unordered_set<std::string> exclude_topic_names = {
        "/rosout",
        "/parameter_events",
    };
};

LaunchProfile generateProfile(const GraphSnapshot& snap,
                              const GenerateOptions& opts,
                              TopicMonitor* topic_monitor = nullptr);

void writeProfileYaml(const LaunchProfile& profile, std::ostream& out);

}  // namespace rtl
