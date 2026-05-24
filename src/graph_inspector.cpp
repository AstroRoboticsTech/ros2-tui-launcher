#include "ros2_tui_launcher/graph_inspector.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>

namespace rtl {

namespace {
std::string joinFullName(const std::string& ns, const std::string& name) {
    if (ns.empty() || ns == "/") {
        return "/" + name;
    }
    if (ns.back() == '/') {
        return ns + name;
    }
    return ns + "/" + name;
}

std::string firstType(const std::vector<std::string>& v) {
    return v.empty() ? std::string{} : v.front();
}
}  // namespace

GraphInspector::GraphInspector(rclcpp::Node::SharedPtr node) : node_(std::move(node)) {}

GraphSnapshot GraphInspector::snapshot(std::chrono::milliseconds settle) {
    if (settle.count() > 0) {
        std::this_thread::sleep_for(settle);
    }

    GraphSnapshot snap;
    snap.captured_at = std::chrono::system_clock::now();

    auto graph = node_->get_node_graph_interface();
    auto graph_nodes_raw = graph->get_node_names_and_namespaces();
    for (const auto& [name, ns] : graph_nodes_raw) {
        GraphNode gn;
        gn.name = name;
        gn.ns = ns;
        gn.full_name = joinFullName(ns, name);

        for (const auto& [topic, types] :
             graph->get_publisher_names_and_types_by_node(name, ns, false)) {
            gn.publishers.emplace_back(topic, firstType(types));
        }
        for (const auto& [topic, types] :
             graph->get_subscriber_names_and_types_by_node(name, ns, false)) {
            gn.subscribers.emplace_back(topic, firstType(types));
        }
        snap.nodes.push_back(std::move(gn));
    }

    std::sort(snap.nodes.begin(), snap.nodes.end(),
              [](const GraphNode& a, const GraphNode& b) { return a.full_name < b.full_name; });

    std::unordered_map<std::string, GraphTopic> topic_map;
    for (const auto& [topic, types] : graph->get_topic_names_and_types(false)) {
        GraphTopic gt;
        gt.name = topic;
        gt.types = types;
        topic_map.emplace(topic, std::move(gt));
    }

    for (const auto& gn : snap.nodes) {
        for (const auto& [topic, _type] : gn.publishers) {
            auto it = topic_map.find(topic);
            if (it != topic_map.end()) {
                it->second.publisher_nodes.push_back(gn.full_name);
            }
        }
        for (const auto& [topic, _type] : gn.subscribers) {
            auto it = topic_map.find(topic);
            if (it != topic_map.end()) {
                it->second.subscriber_nodes.push_back(gn.full_name);
            }
        }
    }

    snap.topics.reserve(topic_map.size());
    for (auto& [_, t] : topic_map) {
        snap.topics.push_back(std::move(t));
    }
    std::sort(snap.topics.begin(), snap.topics.end(),
              [](const GraphTopic& a, const GraphTopic& b) { return a.name < b.name; });

    return snap;
}

}  // namespace rtl
