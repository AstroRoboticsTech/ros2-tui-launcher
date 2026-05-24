ARG ROS_DISTRO=jazzy
FROM ros:${ROS_DISTRO}-ros-base
ARG ROS_DISTRO
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      git \
      pkg-config \
      ca-certificates \
      fakeroot \
      debhelper \
      dh-make \
      devscripts \
      python3-bloom \
      python3-rosdep \
      libproc2-dev \
      libyaml-cpp-dev \
      libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

COPY rosdep-libproc2.yaml /etc/ros/rosdep/rtl.yaml
RUN echo "yaml file:///etc/ros/rosdep/rtl.yaml" > /etc/ros/rosdep/sources.list.d/50-rtl.list \
 && rosdep update --rosdistro=${ROS_DISTRO}

WORKDIR /pkg
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
