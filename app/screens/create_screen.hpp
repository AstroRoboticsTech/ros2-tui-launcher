#pragma once

#include "screen.hpp"
#include "ros2_tui_launcher/graph_inspector.hpp"
#include "ros2_tui_launcher/launch_profile.hpp"

#include <rclcpp/rclcpp.hpp>

#include <deque>
#include <mutex>
#include <string>

namespace rtl::tui {

class CreateScreen : public Screen {
public:
    explicit CreateScreen(rclcpp::Node::SharedPtr node);

    std::string name() const override { return "Create"; }
    std::string hotkey() const override { return "C"; }
    ftxui::Component component() override;

    void setDefaultOutputPath(std::string path) { output_path_ = std::move(path); }

private:
    struct NodeRow {
        std::string label;
        bool selected = true;
        std::string package = "<FILL>";
        std::string executable = "<FILL>";
        GraphNode source;
        ftxui::Component checkbox;
        ftxui::Component package_input;
        ftxui::Component executable_input;
        ftxui::Component line;
    };

    struct TopicRow {
        std::string label;
        bool selected = true;
        std::string type;
        double hz = 0.0;
        GraphTopic source;
        ftxui::Component checkbox;
    };

    void refreshSnapshot();
    void generate();
    void rebuildRows();

    rclcpp::Node::SharedPtr node_;

    std::mutex mutex_;
    GraphSnapshot snap_;
    std::deque<NodeRow> nodes_;
    std::deque<TopicRow> topics_;

    std::string profile_name_ = "generated";
    std::string output_path_ = "./generated.yaml";
    std::string status_ = "Press 'r' to scan the ROS 2 graph";
    bool initial_refresh_done_ = false;

    ftxui::Component cached_component_;
    ftxui::Component node_container_;
    ftxui::Component topic_container_;
    ftxui::Component name_input_;
    ftxui::Component path_input_;
    ftxui::Component refresh_btn_;
    ftxui::Component generate_btn_;
};

}  // namespace rtl::tui
