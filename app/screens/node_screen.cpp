#include "screens/node_screen.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>

using namespace ftxui;

namespace rtl::tui {

NodeScreen::NodeScreen(NodeInspector* inspector)
    : inspector_(inspector) {}

ftxui::Component NodeScreen::component() {
    auto renderer = Renderer([this] {
        std::lock_guard lock(mutex_);

        auto term_size = Terminal::Size();
        int viewport_h = std::max(3, term_size.dimy - 12);
        scroll_list_.setViewportHeight(viewport_h);
        scroll_list_.setItemCount((int)cached_nodes_.size());
        auto [start, end] = scroll_list_.visibleRange();
        int selected = scroll_list_.selected();

        auto header = hbox({
            text("   NODE") | bold | size(WIDTH, EQUAL, 42),
            text("NAMESPACE") | bold | size(WIDTH, EQUAL, 25),
            text("LIFECYCLE") | bold | size(WIDTH, EQUAL, 15),
            text("STATE") | bold | size(WIDTH, EQUAL, 15),
        });

        Elements rows;
        rows.push_back(header);
        rows.push_back(separator());

        for (int idx = start; idx < end; ++idx) {
            const auto& node = cached_nodes_[idx];
            Color lc_color = Color::GrayDark;
            std::string lc_text = "-";
            if (node.is_lifecycle) {
                lc_text = "Yes";
                lc_color = Color::Cyan;
            }

            std::string state_text = node.lifecycle_state.empty() ? "-" : node.lifecycle_state;
            Color state_color = Color::White;
            if (state_text == "active") state_color = Color::Green;
            else if (state_text == "inactive") state_color = Color::Yellow;
            else if (state_text == "unconfigured") state_color = Color::GrayDark;

            bool is_selected = (idx == selected);
            std::string prefix = is_selected ? " > " : "   ";

            auto row = hbox({
                text(prefix + node.name) | size(WIDTH, EQUAL, 42)
                    | (is_selected ? bold : nothing),
                text(node.ns) | dim | size(WIDTH, EQUAL, 25),
                text(lc_text) | color(lc_color) | size(WIDTH, EQUAL, 15),
                text(state_text) | color(state_color) | size(WIDTH, EQUAL, 15),
            });

            if (is_selected) {
                row = row | inverted;
            }

            rows.push_back(row);
        }

        if (cached_nodes_.empty()) {
            rows.push_back(text(" No nodes discovered (is ROS 2 running?)") | dim);
        }

        int total = (int)cached_nodes_.size();
        std::string scroll_info;
        if (total > viewport_h) {
            scroll_info = "  [" + std::to_string(start + 1) + "-" + std::to_string(end)
                        + "/" + std::to_string(total) + "]";
        }

        return vbox({
            vbox(std::move(rows)) | flex,
            separator(),
            hbox({
                text(" " + std::to_string(total) + " nodes  [Up/Down] Select  [r] Refresh" + scroll_info) | dim,
            }),
        });
    });

    return CatchEvent(renderer, [this](Event event) {
        std::lock_guard lock(mutex_);

        // Scroll list handles navigation
        if (scroll_list_.handleEvent(event)) return true;

        // Screen-specific keys
        if (event.is_character() && event.character() == "r") {
            inspector_->refresh();
            return true;
        }
        return false;
    });
}

void NodeScreen::tick() {
    // refresh() is internally throttled to 2s intervals
    inspector_->refresh();
    auto nodes = inspector_->nodes();

    // nodes() comes from an unordered_map, so impose a stable order: lifecycle
    // nodes first (their state is what you watch), then alphabetical by name.
    std::sort(nodes.begin(), nodes.end(),
        [](const DiscoveredNode& a, const DiscoveredNode& b) {
            if (a.is_lifecycle != b.is_lifecycle) return a.is_lifecycle;
            return a.name < b.name;
        });

    std::lock_guard lock(mutex_);
    cached_nodes_ = std::move(nodes);
}

}  // namespace rtl::tui
