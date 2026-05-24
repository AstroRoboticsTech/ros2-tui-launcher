#include "ros2_tui_launcher/host_process_lookup.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <utility>

namespace rtl {

namespace {

std::vector<std::string> readCmdline(const std::string& path) {
    std::vector<std::string> argv;
    std::ifstream f(path, std::ios::binary);
    if (!f) return argv;
    std::string buf((std::istreambuf_iterator<char>(f)), {});
    if (buf.empty()) return argv;
    size_t i = 0;
    while (i < buf.size()) {
        size_t e = buf.find('\0', i);
        if (e == std::string::npos) e = buf.size();
        if (e > i) argv.emplace_back(buf.data() + i, e - i);
        i = e + 1;
    }
    return argv;
}

std::pair<std::string, std::string> extractPackageExecutable(
    const std::vector<std::string>& argv) {
    if (argv.empty()) return {"", ""};
    static const std::regex kRosRe(
        R"((?:/opt/ros/[^/]+|/install)/lib/([^/]+)/([^/\s]+))");
    static const std::regex kColconRe(
        R"(/install/([^/]+)/lib/\1/([^/\s]+))");
    for (const auto& tok : argv) {
        std::smatch m;
        if (std::regex_search(tok, m, kColconRe)) return {m[1].str(), m[2].str()};
        if (std::regex_search(tok, m, kRosRe))    return {m[1].str(), m[2].str()};
    }
    return {"", ""};
}

std::string extractExplicitNodeName(const std::vector<std::string>& argv) {
    static const std::string kNs = "__node:=";
    for (const auto& tok : argv) {
        auto pos = tok.find(kNs);
        if (pos != std::string::npos) return tok.substr(pos + kNs.size());
    }
    return "";
}

}  // namespace

std::vector<HostProcInfo> scanRos2HostProcesses() {
    std::vector<HostProcInfo> out;
    std::error_code ec;
    for (auto& de : std::filesystem::directory_iterator("/proc", ec)) {
        if (ec) break;
        const auto& name = de.path().filename().string();
        if (name.empty() || !std::isdigit(static_cast<unsigned char>(name.front())))
            continue;
        auto argv = readCmdline(de.path().string() + "/cmdline");
        if (argv.empty()) continue;
        auto [pkg, exe] = extractPackageExecutable(argv);
        if (pkg.empty() && exe.empty()) continue;
        HostProcInfo p;
        p.argv = std::move(argv);
        p.package = std::move(pkg);
        p.executable = std::move(exe);
        p.explicit_node_name = extractExplicitNodeName(p.argv);
        out.push_back(std::move(p));
    }
    return out;
}

const HostProcInfo* matchHostProcess(const std::string& bare_name,
                                     const std::string& full_name,
                                     const std::vector<HostProcInfo>& procs) {
    for (const auto& p : procs) {
        if (!p.explicit_node_name.empty() && p.explicit_node_name == full_name)
            return &p;
    }
    for (const auto& p : procs) {
        if (!p.explicit_node_name.empty() && p.explicit_node_name == bare_name)
            return &p;
    }
    for (const auto& p : procs) {
        if (p.explicit_node_name.empty() && p.executable == bare_name)
            return &p;
    }
    return nullptr;
}

}  // namespace rtl
