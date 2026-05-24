cmake_minimum_required(VERSION 3.16)
project(ros2_tui_launcher VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(RTL_BUILD_TESTS "Build unit tests" OFF)
# Install the /usr/bin/rtl wrapper. Only enabled when building the Debian
# package — overlay/devel builds skip it to avoid clobbering an apt-installed
# /usr/bin/rtl. The packaging Dockerfile sets this ON via -DRTL_INSTALL_WRAPPER=ON.
option(RTL_INSTALL_WRAPPER "Install /usr/bin/rtl wrapper (packaging only)" OFF)

# ── ROS 2 Dependencies ──────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(rosgraph_msgs REQUIRED)

# ── Third-party Dependencies ────────────────────────────────────────────────
include(FetchContent)

# SPSCQueue (header-only lock-free single-producer single-consumer queue)
FetchContent_Declare(SPSCQueue
  GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
  GIT_TAG        v1.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SPSCQueue)

# FTXUI v5.0.0
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

# CLI11 — header-only, command-line parser with first-class subcommand support
FetchContent_Declare(CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
  GIT_SHALLOW    TRUE
)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(CLI11)

# yaml-cpp
find_package(yaml-cpp 0.7 QUIET)
if(NOT yaml-cpp_FOUND)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
  )
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(yaml-cpp)
endif()

# spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(spdlog)
endif()

# libproc2 (procps-ng) for system/process monitoring
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPROC2 REQUIRED libproc2)

# NVML (NVIDIA Management Library) — optional, for GPU monitoring without popen
find_library(NVML_LIBRARY nvidia-ml PATHS
  /usr/lib/x86_64-linux-gnu
  /usr/lib/aarch64-linux-gnu
  /usr/lib64
  /usr/local/cuda/lib64
  /usr/local/cuda/targets/aarch64-linux/lib
)
find_path(NVML_INCLUDE_DIR nvml.h PATHS
  /usr/include
  /usr/local/cuda/include
  /usr/local/cuda/targets/aarch64-linux/include
)
if(NVML_LIBRARY AND NVML_INCLUDE_DIR)
  message(STATUS "Found NVML: ${NVML_LIBRARY}")
  set(HAVE_NVML TRUE)
else()
  message(STATUS "NVML not found — GPU monitoring will use nvidia-smi fallback")
  set(HAVE_NVML FALSE)
endif()

# ── Core library (rclcpp-based) ─────────────────────────────────────────────
add_library(ros2_tui_launcher_core STATIC
  src/launch_profile.cpp
  src/process_manager.cpp
  src/log_aggregator.cpp
  src/topic_monitor.cpp
  src/node_inspector.cpp
  src/parameter_manager.cpp
  src/system_monitor.cpp
  src/config_validator.cpp
  src/log_writer.cpp
  src/graph_inspector.cpp
  src/profile_generator.cpp
  src/host_process_lookup.cpp
)
target_include_directories(ros2_tui_launcher_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_include_directories(ros2_tui_launcher_core PUBLIC
  ${LIBPROC2_INCLUDE_DIRS}
)
target_link_libraries(ros2_tui_launcher_core PUBLIC
  yaml-cpp::yaml-cpp
  spdlog::spdlog
  SPSCQueue
  ${LIBPROC2_LIBRARIES}
)
if(HAVE_NVML)
  target_include_directories(ros2_tui_launcher_core PRIVATE ${NVML_INCLUDE_DIR})
  target_link_libraries(ros2_tui_launcher_core PRIVATE ${NVML_LIBRARY})
  target_compile_definitions(ros2_tui_launcher_core PRIVATE RTL_HAS_NVML=1)
endif()
target_compile_options(ros2_tui_launcher_core PRIVATE -Wall -Wextra -Wpedantic)
ament_target_dependencies(ros2_tui_launcher_core PUBLIC
  rclcpp
  rcl_interfaces
  lifecycle_msgs
  rosgraph_msgs
)

# ── TUI library ─────────────────────────────────────────────────────────────
add_library(ros2_tui_launcher_tui STATIC
  app/screens/tui_runner.cpp
  app/screens/launch_screen.cpp
  app/screens/log_screen.cpp
  app/screens/topic_screen.cpp
  app/screens/node_screen.cpp
  app/screens/parameter_screen.cpp
  app/screens/create_screen.cpp
)
target_include_directories(ros2_tui_launcher_tui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/app>
)
target_compile_options(ros2_tui_launcher_tui PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ros2_tui_launcher_tui PUBLIC
  ros2_tui_launcher_core
  ftxui::screen
  ftxui::dom
  ftxui::component
)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(ros2-tui-launcher app/main.cpp)
target_link_libraries(ros2-tui-launcher ros2_tui_launcher_tui CLI11::CLI11)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ros2-tui-launcher DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY include/ DESTINATION include)

# Short-name wrapper installed system-wide at /usr/bin/rtl. Wrapper auto-sources
# the ROS overlay when invoked from a shell that has not, so users can call
# `rtl --config ...` directly after `apt install`. Gated so overlay/devel
# colcon builds do not try to clobber an apt-installed /usr/bin/rtl.
if(RTL_INSTALL_WRAPPER)
  if(NOT DEFINED RTL_ROS_DISTRO)
    if(DEFINED ENV{ROS_DISTRO})
      set(RTL_ROS_DISTRO $ENV{ROS_DISTRO})
    else()
      set(RTL_ROS_DISTRO "jazzy")
    endif()
  endif()
  set(RTL_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/app/rtl.in
    ${CMAKE_CURRENT_BINARY_DIR}/rtl
    @ONLY
  )
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/rtl DESTINATION /usr/bin)
endif()

ament_package()
