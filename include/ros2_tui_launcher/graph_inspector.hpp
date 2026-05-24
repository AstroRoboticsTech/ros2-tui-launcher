#pragma once

#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace rtl {

struct GraphNode {
    std::string name;
    std::string ns;
    std::string full_name;
    std::vector<std::pair<std::string, std::string>> publishers;
    std::vector<std::pair<std::string, std::string>> subscribers;
};

struct GraphTopic {
    std::string name;
    std::vector<std::string> types;
    std::vector<std::string> publisher_nodes;
    std::vector<std::string> subscriber_nodes;
};

struct GraphSnapshot {
    std::vector<GraphNode> nodes;
    std::vector<GraphTopic> topics;
    std::chrono::system_clock::time_point captured_at;
};

class GraphInspector {
public:
    explicit GraphInspector(rclcpp::Node::SharedPtr node);

    GraphSnapshot snapshot(std::chrono::milliseconds settle = std::chrono::milliseconds(500));

private:
    rclcpp::Node::SharedPtr node_;
};

}  // namespace rtl
