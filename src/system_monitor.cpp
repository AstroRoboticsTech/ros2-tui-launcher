#include "ros2_tui_launcher/system_monitor.hpp"

#ifdef RTL_HAS_NVML
#include <nvml.h>
#endif

#include <spdlog/spdlog.h>

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace rtl {

namespace {

// Read an entire (small) sysfs/procfs file and trim surrounding whitespace and
// trailing NULs (device-tree strings are NUL-terminated).
std::string readTrim(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    auto is_junk = [](char c) {
        return c == '\0' || c == '\n' || c == '\r' || std::isspace(static_cast<unsigned char>(c));
    };
    while (!s.empty() && is_junk(s.back())) s.pop_back();
    std::size_t start = 0;
    while (start < s.size() && is_junk(s[start])) ++start;
    return s.substr(start);
}

// Jetson/Tegra boards always ship this file (L4T release manifest).
bool isJetson() {
    std::error_code ec;
    return std::filesystem::exists("/etc/nv_tegra_release", ec);
}

// Locate the integrated-GPU load file. Path differs by SoC generation
// (gpu.0 on older L4T, <addr>.gpu on Orin/Xavier), so probe then scan.
std::string findTegraLoadPath() {
    if (std::filesystem::exists("/sys/devices/platform/gpu.0/load"))
        return "/sys/devices/platform/gpu.0/load";
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator("/sys/devices/platform", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".gpu") {
            auto candidate = entry.path() / "load";
            if (std::filesystem::exists(candidate)) return candidate.string();
        }
    }
    return {};
}

// Find the thermal zone whose `type` mentions the GPU (e.g. "gpu-thermal" on
// Orin, "GPU-therm" on older JetPacks). Returns its `temp` file path.
std::string findTegraTempPath() {
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator("/sys/class/thermal", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("thermal_zone", 0) != 0) continue;
        std::string type = readTrim((entry.path() / "type").string());
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (type.find("gpu") != std::string::npos)
            return (entry.path() / "temp").string();
    }
    return {};
}

}  // namespace

namespace {

// Page size in KiB, for converting /proc/<pid>/stat RSS (in pages) to KiB.
const long kPageSizeKb = sysconf(_SC_PAGESIZE) / 1024;

// Parsed subset of /proc/<pid>/stat that we care about.
struct ProcStat {
    pid_t ppid = 0;
    char state = '?';
    unsigned long long tics = 0;   // utime + stime, in clock ticks
    unsigned long rss_kb = 0;
    std::string comm;
};

// Parse /proc/<pid>/stat. The comm field (2) is wrapped in parentheses and may
// itself contain spaces or ')', so split on the LAST ')' and index the numeric
// tail from field 3 onward. Returns false if the file can't be read/parsed.
bool parseProcStat(pid_t pid, ProcStat& out) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    std::getline(f, line);
    if (line.empty()) return false;

    auto open_paren = line.find('(');
    auto close_paren = line.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos ||
        close_paren < open_paren)
        return false;

    out.comm = line.substr(open_paren + 1, close_paren - open_paren - 1);

    // Tokenize everything after ") " — these are fields 3..N.
    std::vector<std::string> t;
    t.reserve(52);
    std::istringstream iss(line.substr(close_paren + 1));
    for (std::string tok; iss >> tok;) t.push_back(std::move(tok));

    // Field N maps to t[N-3]. Need state(3), ppid(4), utime(14), stime(15), rss(24).
    if (t.size() < 22) return false;
    out.state = t[0].empty() ? '?' : t[0][0];
    try {
        out.ppid = static_cast<pid_t>(std::stol(t[1]));
        unsigned long long utime = std::stoull(t[11]);
        unsigned long long stime = std::stoull(t[12]);
        out.tics = utime + stime;
        unsigned long rss_pages = std::stoul(t[21]);
        out.rss_kb = rss_pages * static_cast<unsigned long>(kPageSizeKb);
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace

SystemMonitor::SystemMonitor() {
    hertz_ = sysconf(_SC_CLK_TCK);
    num_cpus_ = sysconf(_SC_NPROCESSORS_ONLN);
    if (hertz_ <= 0) hertz_ = 100;
    if (num_cpus_ <= 0) num_cpus_ = 1;

    detectSystemInfo();
}

SystemMonitor::~SystemMonitor() {
#ifdef RTL_HAS_NVML
    if (nvml_initialized_) {
        nvmlShutdown();
    }
#endif
}

void SystemMonitor::detectSystemInfo() {
    // CPU model from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    int physical_ids = 0;
    std::string model;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") == 0) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                model = line.substr(pos + 2);
            }
        }
        if (line.find("cpu cores") == 0) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                try { physical_ids = std::stoi(line.substr(pos + 2)); } catch (...) {}
            }
        }
    }

    std::lock_guard lock(mutex_);
    cached_system_.cpu_model = model;
    cached_system_.cpu_cores = physical_ids > 0 ? physical_ids : static_cast<int>(num_cpus_);
    cached_system_.cpu_threads = static_cast<int>(num_cpus_);

    // GPU detection — on Jetson/Tegra the integrated GPU has no usable NVML and
    // nvidia-smi reports N/A, so read sysfs directly. This takes priority.
    if (isJetson()) {
        tegra_load_path_ = findTegraLoadPath();
        tegra_temp_path_ = findTegraTempPath();
        if (!tegra_load_path_.empty()) {
            tegra_gpu_ = true;
            cached_system_.has_gpu = true;
            std::string model = readTrim("/proc/device-tree/model");
            cached_system_.gpu_name = model.empty() ? "Jetson iGPU" : model + " (iGPU)";
            // Integrated GPU shares system RAM (unified memory). Mirror total now;
            // used is refreshed alongside system memory in refreshGpu().
            cached_system_.gpu_mem_total_mb = cached_system_.mem_total_kb / 1024;
            spdlog::info("SystemMonitor: Tegra GPU via sysfs (load={}, temp={})",
                         tegra_load_path_,
                         tegra_temp_path_.empty() ? "n/a" : tegra_temp_path_);
        }
    }

    // prefer NVML, fall back to nvidia-smi with timeout
#ifdef RTL_HAS_NVML
    if (!tegra_gpu_ && nvmlInit_v2() == NVML_SUCCESS) {
        nvmlDevice_t device;
        if (nvmlDeviceGetHandleByIndex(0, &device) == NVML_SUCCESS) {
            nvml_initialized_ = true;
            nvml_device_ = reinterpret_cast<void*>(device);

            char name_buf[NVML_DEVICE_NAME_V2_BUFFER_SIZE];
            if (nvmlDeviceGetName(device, name_buf, sizeof(name_buf)) == NVML_SUCCESS) {
                cached_system_.gpu_name = name_buf;
            }

            nvmlMemory_t mem;
            if (nvmlDeviceGetMemoryInfo(device, &mem) == NVML_SUCCESS) {
                cached_system_.gpu_mem_total_mb = mem.total / (1024 * 1024);
            }
            cached_system_.has_gpu = true;
        } else {
            nvmlShutdown();
        }
    }
#endif

    // Fallback: nvidia-smi with timeout (only if NVML and Tegra unavailable)
    if (!tegra_gpu_ && !nvml_initialized_) {
        FILE* fp = popen("timeout 2 nvidia-smi --query-gpu=name,memory.total "
                         "--format=csv,noheader,nounits 2>/dev/null", "r");
        if (fp) {
            char buf[256];
            if (fgets(buf, sizeof(buf), fp)) {
                std::string result(buf);
                auto comma = result.find(',');
                if (comma != std::string::npos) {
                    cached_system_.gpu_name = result.substr(0, comma);
                    while (!cached_system_.gpu_name.empty() && cached_system_.gpu_name.back() == ' ')
                        cached_system_.gpu_name.pop_back();
                    try {
                        cached_system_.gpu_mem_total_mb = std::stoul(result.substr(comma + 1));
                    } catch (...) {}
                    cached_system_.has_gpu = true;
                }
            }
            pclose(fp);
        }
    }
}

void SystemMonitor::refresh() {
    auto now = std::chrono::steady_clock::now();

    bool do_proc = false;
    bool do_gpu = false;
    {
        std::lock_guard lock(mutex_);
        do_proc = (now - last_proc_refresh_ >= kProcInterval);
        do_gpu = cached_system_.has_gpu && (now - last_gpu_refresh_ >= kGpuInterval);
        if (do_proc) last_proc_refresh_ = now;
        if (do_gpu) last_gpu_refresh_ = now;
    }

    if (do_proc) {
        refreshSystemCpu();   // Must come first — single stat read, computes deltas
        refreshProcesses();   // Uses delta_total from refreshSystemCpu()
        refreshSystemMem();
    }

    if (do_gpu) {
        refreshGpu();
    }
}

void SystemMonitor::refreshProcesses() {
    // Enumerate numeric entries in /proc — one directory per live PID.
    DIR* proc = opendir("/proc");
    if (!proc) return;

    // Use the delta_total computed by refreshSystemCpu() (called first).
    unsigned long long delta_total;
    {
        std::lock_guard lock(mutex_);
        delta_total = delta_total_ticks_;
    }

    std::lock_guard lock(mutex_);

    std::unordered_map<pid_t, ProcessStats> new_procs;
    std::unordered_map<pid_t, unsigned long long> new_ticks;

    struct dirent* ent;
    while ((ent = readdir(proc)) != nullptr) {
        // Skip non-numeric names (only /proc/<pid> dirs interest us).
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        pid_t pid = static_cast<pid_t>(std::atol(ent->d_name));
        if (pid <= 0) continue;

        ProcStat st;
        if (!parseProcStat(pid, st)) continue;   // process likely exited mid-scan

        ProcessStats ps;
        ps.pid = pid;
        ps.ppid = st.ppid;
        ps.comm = st.comm;
        ps.mem_rss_kb = st.rss_kb;
        ps.state = st.state;

        // CPU% = delta_proc_ticks / delta_total_ticks * 100 * num_cpus
        auto prev_it = prev_proc_ticks_.find(pid);
        if (prev_it != prev_proc_ticks_.end() && delta_total > 0) {
            unsigned long long delta_proc = st.tics - prev_it->second;
            ps.cpu_percent = static_cast<double>(delta_proc) / static_cast<double>(delta_total) * 100.0 * num_cpus_;
            if (ps.cpu_percent > 100.0 * num_cpus_) ps.cpu_percent = 100.0 * num_cpus_;
            if (ps.cpu_percent < 0.0) ps.cpu_percent = 0.0;
        }

        // Attach GPU memory if available
        auto gpu_it = gpu_proc_mem_.find(pid);
        if (gpu_it != gpu_proc_mem_.end()) {
            ps.gpu_mem_mb = gpu_it->second;
        }

        new_ticks[pid] = st.tics;
        new_procs[pid] = std::move(ps);
    }
    closedir(proc);

    cached_procs_ = std::move(new_procs);
    prev_proc_ticks_ = std::move(new_ticks);

    // Build ppid→children index for O(1) lookups in buildTree()
    children_index_.clear();
    children_index_.reserve(cached_procs_.size());
    for (const auto& [child_pid, child_stats] : cached_procs_) {
        if (child_pid != child_stats.ppid) {
            children_index_[child_stats.ppid].push_back(child_pid);
        }
    }
}

void SystemMonitor::refreshSystemCpu() {
    // First line of /proc/stat: "cpu  user nice system idle iowait irq softirq
    // steal guest guest_nice". guest/guest_nice are already counted in user/nice,
    // so summing the first 8 columns gives total without double-counting.
    std::ifstream f("/proc/stat");
    if (!f) return;
    std::string cpu_label;
    f >> cpu_label;
    if (cpu_label != "cpu") return;

    unsigned long long vals[8] = {0};
    int n = 0;
    for (; n < 8 && (f >> vals[n]); ++n) {}
    if (n < 4) return;  // need at least user/nice/system/idle

    unsigned long long idle_all = vals[3] + vals[4];  // idle + iowait
    unsigned long long total_ticks = 0;
    for (int i = 0; i < n; ++i) total_ticks += vals[i];
    unsigned long long busy_ticks = total_ticks - idle_all;

    unsigned long long delta_total = total_ticks - prev_total_ticks_;
    unsigned long long delta_busy = busy_ticks - prev_busy_ticks_;

    std::lock_guard lock(mutex_);

    // Store delta_total for per-process CPU% calculation in refreshProcesses()
    delta_total_ticks_ = delta_total;

    if (prev_total_ticks_ > 0 && delta_total > 0) {
        cached_system_.cpu_usage_percent =
            static_cast<double>(delta_busy) / static_cast<double>(delta_total) * 100.0;
        cached_system_.cpu_usage_percent = std::clamp(cached_system_.cpu_usage_percent, 0.0, 100.0);
    }

    prev_total_ticks_ = total_ticks;
    prev_busy_ticks_ = busy_ticks;
}

void SystemMonitor::refreshSystemMem() {
    std::ifstream f("/proc/meminfo");
    if (!f) return;

    unsigned long total = 0, free_kb = 0, avail = 0, buffers = 0,
                  cached = 0, sreclaim = 0, shmem = 0;
    bool have_avail = false;
    std::string key;
    unsigned long val;
    std::string unit;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:")          total = val;
        else if (key == "MemFree:")      free_kb = val;
        else if (key == "MemAvailable:") { avail = val; have_avail = true; }
        else if (key == "Buffers:")      buffers = val;
        else if (key == "Cached:")       cached = val;
        else if (key == "SReclaimable:") sreclaim = val;
        else if (key == "Shmem:")        shmem = val;
    }

    // procps (and the prior libproc2 path) define used = MemTotal - MemAvailable,
    // which counts reclaimable cache/buffers as free. Match that exactly. On very
    // old kernels without MemAvailable, fall back to the classic free(1) estimate.
    unsigned long used;
    if (have_avail) {
        used = (total > avail) ? (total - avail) : 0;
    } else {
        unsigned long cache_total = cached + sreclaim;
        if (cache_total >= shmem) cache_total -= shmem; else cache_total = 0;
        unsigned long non_used = free_kb + buffers + cache_total;
        used = (total > non_used) ? (total - non_used) : 0;
        avail = free_kb + buffers + cache_total;
    }

    std::lock_guard lock(mutex_);
    cached_system_.mem_total_kb     = total;
    cached_system_.mem_used_kb      = used;
    cached_system_.mem_available_kb = avail;
}

void SystemMonitor::refreshGpu() {
    // Tegra/Jetson: integrated GPU stats from sysfs (no fork, no NVML).
    if (tegra_gpu_) {
        std::string load_raw = readTrim(tegra_load_path_);
        std::string temp_raw = tegra_temp_path_.empty() ? "" : readTrim(tegra_temp_path_);
        std::lock_guard lock(mutex_);
        if (!load_raw.empty()) {
            try {
                // sysfs load is per-mille (0..1000) → percent.
                cached_system_.gpu_utilization =
                    std::clamp(std::stod(load_raw) / 10.0, 0.0, 100.0);
            } catch (...) {}
        }
        if (!temp_raw.empty()) {
            try {
                cached_system_.gpu_temp_c = std::stod(temp_raw) / 1000.0;
            } catch (...) {}
        }
        // Unified memory: GPU "VRAM" is system RAM.
        cached_system_.gpu_mem_total_mb = cached_system_.mem_total_kb / 1024;
        cached_system_.gpu_mem_used_mb = cached_system_.mem_used_kb / 1024;
        // Per-process GPU memory is not exposed without NVML on Tegra.
        return;
    }

#ifdef RTL_HAS_NVML
    if (nvml_initialized_) {
        auto device = reinterpret_cast<nvmlDevice_t>(nvml_device_);

        // System GPU stats via NVML (no fork/exec overhead)
        nvmlMemory_t mem;
        if (nvmlDeviceGetMemoryInfo(device, &mem) == NVML_SUCCESS) {
            std::lock_guard lock(mutex_);
            cached_system_.gpu_mem_used_mb = mem.used / (1024 * 1024);
        }

        nvmlUtilization_t util;
        if (nvmlDeviceGetUtilizationRates(device, &util) == NVML_SUCCESS) {
            std::lock_guard lock(mutex_);
            cached_system_.gpu_utilization = static_cast<double>(util.gpu);
        }

        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
            std::lock_guard lock(mutex_);
            cached_system_.gpu_temp_c = static_cast<double>(temp);
        }

        // Per-process GPU memory via NVML
        unsigned int info_count = 0;
        // First call to get count
        nvmlDeviceGetComputeRunningProcesses(device, &info_count, nullptr);
        if (info_count > 0) {
            std::vector<nvmlProcessInfo_t> procs(info_count);
            if (nvmlDeviceGetComputeRunningProcesses(device, &info_count, procs.data()) == NVML_SUCCESS) {
                std::unordered_map<pid_t, unsigned long> new_gpu;
                for (unsigned int i = 0; i < info_count; ++i) {
                    new_gpu[static_cast<pid_t>(procs[i].pid)] =
                        procs[i].usedGpuMemory / (1024 * 1024);
                }
                std::lock_guard lock(mutex_);
                gpu_proc_mem_ = std::move(new_gpu);
            }
        } else {
            std::lock_guard lock(mutex_);
            gpu_proc_mem_.clear();
        }
        return;
    }
#endif

    // Fallback: nvidia-smi via popen (with timeout to avoid hanging)
    FILE* fp = popen("timeout 2 nvidia-smi --query-gpu=memory.used,utilization.gpu,temperature.gpu "
                     "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            unsigned long mem_used = 0;
            double util = 0, temp = 0;
            if (sscanf(buf, "%lu, %lf, %lf", &mem_used, &util, &temp) >= 1) {
                std::lock_guard lock(mutex_);
                cached_system_.gpu_mem_used_mb = mem_used;
                cached_system_.gpu_utilization = util;
                cached_system_.gpu_temp_c = temp;
            }
        }
        pclose(fp);
    }

    // Per-process GPU memory
    fp = popen("timeout 2 nvidia-smi --query-compute-apps=pid,used_gpu_memory "
               "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (fp) {
        std::unordered_map<pid_t, unsigned long> new_gpu;
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            pid_t pid = 0;
            unsigned long mem = 0;
            if (sscanf(buf, "%d, %lu", &pid, &mem) == 2 && pid > 0) {
                new_gpu[pid] = mem;
            }
        }
        pclose(fp);

        std::lock_guard lock(mutex_);
        gpu_proc_mem_ = std::move(new_gpu);
    }
}

SystemInfo SystemMonitor::systemInfo() const {
    std::lock_guard lock(mutex_);
    return cached_system_;
}

ProcessStats SystemMonitor::processStats(pid_t pid) const {
    std::lock_guard lock(mutex_);
    auto it = cached_procs_.find(pid);
    if (it == cached_procs_.end()) return {};
    return it->second;
}

ProcessTreeNode SystemMonitor::processTree(pid_t root_pid) const {
    std::lock_guard lock(mutex_);
    return buildTree(root_pid);
}

ProcessTreeNode SystemMonitor::buildTree(pid_t pid) const {
    ProcessTreeNode node;

    auto it = cached_procs_.find(pid);
    if (it != cached_procs_.end()) {
        node.stats = it->second;
    } else {
        node.stats.pid = pid;
    }

    node.total_cpu_percent = node.stats.cpu_percent;
    node.total_mem_rss_kb = node.stats.mem_rss_kb;
    node.total_gpu_mem_mb = node.stats.gpu_mem_mb;

    // Use pre-built children index for O(1) lookup instead of O(n) scan
    auto children_it = children_index_.find(pid);
    if (children_it != children_index_.end()) {
        for (pid_t child_pid : children_it->second) {
            auto child_node = buildTree(child_pid);
            node.total_cpu_percent += child_node.total_cpu_percent;
            node.total_mem_rss_kb += child_node.total_mem_rss_kb;
            node.total_gpu_mem_mb += child_node.total_gpu_mem_mb;
            node.children.push_back(std::move(child_node));
        }
    }

    // Sort children by PID for stable display
    std::sort(node.children.begin(), node.children.end(),
        [](const ProcessTreeNode& a, const ProcessTreeNode& b) {
            return a.stats.pid < b.stats.pid;
        });

    return node;
}

}  // namespace rtl
