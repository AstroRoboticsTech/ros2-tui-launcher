#pragma once

#include <string>
#include <vector>

namespace rtl {

struct HostProcInfo {
    std::vector<std::string> argv;
    std::string package;
    std::string executable;
    std::string explicit_node_name;
};

std::vector<HostProcInfo> scanRos2HostProcesses();

const HostProcInfo* matchHostProcess(const std::string& bare_name,
                                     const std::string& full_name,
                                     const std::vector<HostProcInfo>& procs);

}  // namespace rtl
