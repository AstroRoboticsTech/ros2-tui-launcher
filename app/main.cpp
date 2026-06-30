#include "screens/tui_runner.hpp"
#include "screens/launch_screen.hpp"
#include "screens/log_screen.hpp"
#include "screens/topic_screen.hpp"
#include "screens/node_screen.hpp"
#include "screens/parameter_screen.hpp"
#include "screens/create_screen.hpp"

#include "ros2_tui_launcher/launch_profile.hpp"
#include "ros2_tui_launcher/process_manager.hpp"
#include "ros2_tui_launcher/log_aggregator.hpp"
#include "ros2_tui_launcher/topic_monitor.hpp"
#include "ros2_tui_launcher/node_inspector.hpp"
#include "ros2_tui_launcher/parameter_manager.hpp"
#include "ros2_tui_launcher/system_monitor.hpp"
#include "ros2_tui_launcher/log_writer.hpp"
#include "ros2_tui_launcher/graph_inspector.hpp"
#include "ros2_tui_launcher/profile_generator.hpp"
#include "ros2_tui_launcher/config_validator.hpp"

#include <CLI/CLI.hpp>
#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<rtl::tui::TuiRunner*> g_tui{nullptr};

void signalHandler(int) {
    auto* tui = g_tui.load(std::memory_order_acquire);
    if (tui) {
        tui->requestStop();
    }
}

// Split argv on "--ros-args": everything from that token onward is forwarded
// to rclcpp; everything before is for our own CLI parser.
std::pair<std::vector<char*>, std::vector<char*>>
splitRosArgs(int argc, char* argv[]) {
    std::vector<char*> ours;
    std::vector<char*> ros;
    ours.push_back(argv[0]);
    ros.push_back(argv[0]);
    bool ros_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (!ros_mode && std::string_view(argv[i]) == "--ros-args") {
            ros_mode = true;
        }
        if (ros_mode) {
            ros.push_back(argv[i]);
        } else {
            ours.push_back(argv[i]);
        }
    }
    return {ours, ros};
}

int runTui(const std::string& profile_dir,
           const std::string& profile_file,
           std::vector<char*> ros_argv) {
    rclcpp::InitOptions init_options;
    init_options.shutdown_on_signal = false;
    int ros_argc = static_cast<int>(ros_argv.size());
    rclcpp::init(ros_argc, ros_argv.data(), init_options);
    auto node = rclcpp::Node::make_shared("ros2_tui_launcher");

    std::vector<rtl::LaunchProfile> profiles;
    if (!profile_file.empty()) {
        try {
            profiles.push_back(rtl::loadProfile(profile_file));
        } catch (const std::exception& e) {
            spdlog::error("Failed to load profile '{}': {}", profile_file, e.what());
            rclcpp::shutdown();
            return 1;
        }
    } else {
        profiles = rtl::discoverProfiles(profile_dir);
    }

    if (profiles.empty()) {
        spdlog::warn("No profiles found. Use --profiles <dir> or --config <file>.");
        spdlog::info("Creating empty default profile.");
        rtl::LaunchProfile empty;
        empty.name = "Empty";
        empty.description = "No profile loaded";
        profiles.push_back(std::move(empty));
    }

    int active_profile = 0;

    rtl::ProcessManager proc_mgr;
    rtl::LogAggregator log_agg(node);
    rtl::TopicMonitor topic_mon(node);
    rtl::NodeInspector node_inspector(node);
    rtl::ParameterManager param_mgr(node);
    rtl::SystemMonitor sys_mon;

    auto session_ts = rtl::LogWriter::sessionTimestamp();
    auto log_writer = std::make_shared<rtl::LogWriter>(
        profiles[active_profile].log_config,
        profiles[active_profile].name,
        session_ts);
    log_agg.setLogWriter(log_writer);

    proc_mgr.setLogCallback([&log_agg](const std::string& source, const std::string& line) {
        log_agg.pushRaw(source, line);
    });

    if (!profiles[active_profile].monitored_topics.empty()) {
        std::vector<std::pair<std::string, double>> watched;
        for (const auto& mt : profiles[active_profile].monitored_topics) {
            watched.emplace_back(mt.topic, mt.expected_hz);
        }
        topic_mon.setWatchedTopics(watched);
    }

    topic_mon.start();

    std::thread spin_thread([&node] {
        rclcpp::spin(node);
    });

    for (const auto& entry : profiles[active_profile].entries) {
        if (entry.autostart) {
            proc_mgr.start(entry);
        }
    }

    rtl::tui::TuiRunner tui("ros2-tui-launcher");
    g_tui.store(&tui, std::memory_order_release);

    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    auto launch_screen = std::make_shared<rtl::tui::LaunchScreen>(
        &profiles, &active_profile, &proc_mgr, &sys_mon);
    launch_screen->setProfileChangeCallback(
        [&profiles, &log_agg, &log_writer, &session_ts](int new_idx) {
            log_writer = std::make_shared<rtl::LogWriter>(
                profiles[new_idx].log_config,
                profiles[new_idx].name,
                session_ts);
            log_agg.setLogWriter(log_writer);
        });
    tui.addScreen(launch_screen);
    tui.addScreen<rtl::tui::LogScreen>(&log_agg, &node_inspector);
    tui.addScreen<rtl::tui::TopicScreen>(&topic_mon, &node_inspector);
    tui.addScreen<rtl::tui::NodeScreen>(&node_inspector);
    tui.addScreen<rtl::tui::ParameterScreen>(&node_inspector, &param_mgr);
    tui.addScreen<rtl::tui::CreateScreen>(node);

    try {
        tui.run();
    } catch (const std::exception& e) {
        spdlog::error("TUI error: {}", e.what());
    }

    g_tui.store(nullptr, std::memory_order_release);

    spdlog::info("Shutting down...");
    topic_mon.stop();
    proc_mgr.stopAll();
    rclcpp::shutdown();
    if (spin_thread.joinable()) {
        spin_thread.join();
    }
    return 0;
}

struct GenerateArgs {
    std::string output;
    std::string name;
    double sample_seconds = 3.0;
    bool skip_topics = false;
    bool skip_nodes = false;
    bool launch_after = false;
};

int runConfigGenerate(const GenerateArgs& args, std::vector<char*> ros_argv) {
    rclcpp::InitOptions init_options;
    init_options.shutdown_on_signal = false;
    int ros_argc = static_cast<int>(ros_argv.size());
    rclcpp::init(ros_argc, ros_argv.data(), init_options);
    auto node = rclcpp::Node::make_shared("ros2_tui_launcher_config_generate");

    std::thread spin_thread([&node] { rclcpp::spin(node); });

    auto settle = std::chrono::milliseconds(
        static_cast<int64_t>(args.sample_seconds * 1000.0));

    rtl::GraphInspector inspector(node);
    auto snap = inspector.snapshot(settle);

    std::unique_ptr<rtl::TopicMonitor> topic_mon;
    if (!args.skip_topics && !snap.topics.empty()) {
        topic_mon = std::make_unique<rtl::TopicMonitor>(
            node, std::chrono::milliseconds(500));
        std::vector<std::pair<std::string, double>> watched;
        for (const auto& gt : snap.topics) {
            watched.emplace_back(gt.name, 0.0);
        }
        topic_mon->setWatchedTopics(watched);
        topic_mon->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::max<int64_t>(1000, settle.count())));
    }

    rtl::GenerateOptions opts;
    opts.profile_name = args.name;
    opts.include_topics = !args.skip_topics;
    opts.include_nodes = !args.skip_nodes;

    auto profile = rtl::generateProfile(snap, opts, topic_mon.get());

    if (topic_mon) topic_mon->stop();

    std::cerr << "# rtl config generate: profile '" << profile.name
              << "' with " << profile.entries.size() << " entries, "
              << profile.monitored_topics.size() << " monitored topics\n";

    if (!args.output.empty()) {
        std::ofstream f(args.output);
        if (!f) {
            std::cerr << "ERROR: cannot open '" << args.output << "' for writing\n";
            rclcpp::shutdown();
            if (spin_thread.joinable()) spin_thread.join();
            return 1;
        }
        rtl::writeProfileYaml(profile, f);
        std::cerr << "# Wrote " << args.output << "\n";
    } else {
        rtl::writeProfileYaml(profile, std::cout);
    }

    rclcpp::shutdown();
    if (spin_thread.joinable()) spin_thread.join();

    if (args.launch_after && !args.output.empty()) {
        return runTui(".", args.output, {});
    }
    return 0;
}

int runConfigNew(std::vector<char*> ros_argv) {
    rclcpp::InitOptions init_options;
    init_options.shutdown_on_signal = false;
    int ros_argc = static_cast<int>(ros_argv.size());
    rclcpp::init(ros_argc, ros_argv.data(), init_options);
    auto node = rclcpp::Node::make_shared("ros2_tui_launcher_config_new");

    std::thread spin_thread([&node] { rclcpp::spin(node); });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    rtl::tui::TuiRunner tui("rtl config new");
    g_tui.store(&tui, std::memory_order_release);

    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    tui.addScreen<rtl::tui::CreateScreen>(node);

    try {
        tui.run();
    } catch (const std::exception& e) {
        spdlog::error("TUI error: {}", e.what());
    }

    g_tui.store(nullptr, std::memory_order_release);
    rclcpp::shutdown();
    if (spin_thread.joinable()) spin_thread.join();
    return 0;
}

int runConfigValidate(const std::string& file) {
    rtl::LaunchProfile profile;
    try {
        profile = rtl::loadProfile(file);
    } catch (const std::exception& e) {
        std::cerr << file << ": parse error: " << e.what() << "\n";
        return 1;
    }

    rtl::ConfigValidator validator;
    auto result = validator.validate(profile, file);

    if (result.errors.empty()) {
        std::cout << file << ": OK (" << profile.entries.size() << " entries, "
                  << profile.monitored_topics.size() << " monitored topics)\n";
        return 0;
    }

    for (const auto& err : result.errors) {
        std::cerr << file << ": "
                  << (err.critical ? "ERROR" : "WARN") << ": "
                  << (err.entry_name.empty() ? "" : err.entry_name + ": ")
                  << err.field << ": " << err.message << "\n";
    }
    return result.valid ? 0 : 1;
}

int runConfigList(const std::string& dir) {
    auto profiles = rtl::discoverProfiles(dir);
    if (profiles.empty()) {
        std::cerr << "No profiles found in " << dir << "\n";
        return 1;
    }
    for (const auto& p : profiles) {
        std::cout << p.name
                  << "  entries=" << p.entries.size()
                  << "  monitored=" << p.monitored_topics.size()
                  << (p.description.empty() ? "" : "  -- " + p.description)
                  << "\n";
    }
    return 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    auto [our_argv, ros_argv] = splitRosArgs(argc, argv);

    CLI::App app{"ros2-tui-launcher - ROS 2 Launch TUI Manager"};
    app.name("rtl");
    app.set_help_flag("-h,--help", "Show this help and exit");
    app.set_version_flag("-V,--version", std::string("rtl 0.3.0"));
    app.require_subcommand(0, 1);
    app.footer(
        "Examples:\n"
        "  rtl                                 Launch the TUI (current directory profiles)\n"
        "  rtl tui --profiles ./profiles       Launch with a profile directory\n"
        "  rtl config my-profile.yaml          Open one YAML in the TUI\n"
        "  rtl config generate -o gen.yaml     Scaffold from the live ROS 2 graph\n"
        "  rtl config new                      Interactive Create screen\n"
        "  rtl config validate gen.yaml        Schema-check\n"
        "  rtl config list -p ./profiles       List profiles in a directory\n"
        "\n"
        "TUI hotkeys:\n"
        "  [L] Launch  [G] Logs  [T] Topics  [N] Nodes  [P] Params  [C] Create  [Q] Quit\n"
        "\n"
        "Anything after '--ros-args' is forwarded verbatim to rclcpp.");

    auto* tui_cmd = app.add_subcommand("tui", "Launch the TUI (same as no subcommand)");
    std::string tui_profiles = ".";
    std::string tui_config;
    tui_cmd->add_option("-p,--profiles", tui_profiles,
                        "Directory containing launch profile YAMLs")
        ->check(CLI::ExistingDirectory);
    tui_cmd->add_option("-c,--config", tui_config,
                        "Single profile YAML to load")
        ->check(CLI::ExistingFile);

    auto* config_cmd = app.add_subcommand("config", "Manage launch profiles");
    std::string config_file_arg;
    config_cmd->add_option("file", config_file_arg,
                           "Profile YAML to open in the TUI "
                           "(shorthand for `rtl tui --config <FILE>`)")
        ->check(CLI::ExistingFile);

    auto* gen_cmd = config_cmd->add_subcommand(
        "generate", "Scaffold a profile from the live ROS 2 graph");
    std::string gen_output;
    std::string gen_name;
    double gen_sample_seconds = 3.0;
    bool gen_skip_topics = false;
    bool gen_skip_nodes = false;
    bool gen_launch = false;
    gen_cmd->add_option("-o,--output", gen_output,
                        "Where to write the YAML (default: stdout)");
    gen_cmd->add_option("--name", gen_name,
                        "Profile name (default: generated-<timestamp>)");
    gen_cmd->add_option("--sample-seconds", gen_sample_seconds,
                        "How long to observe the graph before emitting (default: 3.0)");
    gen_cmd->add_flag("--no-topics", gen_skip_topics, "Skip the monitored_topics section");
    gen_cmd->add_flag("--no-nodes", gen_skip_nodes, "Skip the entries section");
    gen_cmd->add_flag("--launch", gen_launch,
                      "After writing, open the new profile in the TUI");

    auto* new_cmd = config_cmd->add_subcommand(
        "new", "Open the interactive Create screen and scaffold a profile from the graph");

    auto* validate_cmd = config_cmd->add_subcommand("validate", "Validate a profile YAML");
    std::string validate_file;
    validate_cmd->add_option("file", validate_file, "Profile YAML to validate")
        ->required()
        ->check(CLI::ExistingFile);

    auto* list_cmd = config_cmd->add_subcommand(
        "list", "List profiles found in a directory");
    std::string list_dir = ".";
    list_cmd->add_option("-p,--profiles", list_dir, "Directory to scan")
        ->check(CLI::ExistingDirectory);

    int our_argc = static_cast<int>(our_argv.size());
    CLI11_PARSE(app, our_argc, our_argv.data());

    if (gen_cmd->parsed()) {
        GenerateArgs gargs{
            gen_output, gen_name, gen_sample_seconds,
            gen_skip_topics, gen_skip_nodes, gen_launch};
        return runConfigGenerate(gargs, std::move(ros_argv));
    }
    if (new_cmd->parsed())      return runConfigNew(std::move(ros_argv));
    if (validate_cmd->parsed()) return runConfigValidate(validate_file);
    if (list_cmd->parsed())     return runConfigList(list_dir);

    std::string profiles_dir = ".";
    std::string config_file;
    if (tui_cmd->parsed()) {
        profiles_dir = tui_profiles;
        config_file = tui_config;
    } else if (config_cmd->parsed() && !config_file_arg.empty()) {
        config_file = config_file_arg;
    }

    return runTui(profiles_dir, config_file, std::move(ros_argv));
}
