FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV QT_QPA_PLATFORM=xcb

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    iproute2 \
    iputils-ping \
    net-tools \
    tcpdump \
    procps \
    vim \
    bash \
    sudo \
    util-linux \
    libpcap-dev \
    openvswitch-switch \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-qpa-plugins \
    libqt6core6 \
    libqt6gui6 \
    libqt6network6 \
    libqt6widgets6 \
    libx11-xcb1 \
    libxcb1 \
    libxcb-cursor0 \
    libxcb-keysyms1 \
    libxcb-image0 \
    libxcb-shm0 \
    libxcb-icccm4 \
    libxcb-sync1 \
    libxcb-render-util0 \
    libxcb-randr0 \
    libxcb-shape0 \
    libxcb-xfixes0 \
    libxcb-xinerama0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/ovs_fc_topology
COPY . /src/ovs_fc_topology

RUN cmake -S . -B /build/ovs_fc_topology \
    && cmake --build /build/ovs_fc_topology -j$(nproc) \
    && mkdir -p /app \
    && cp /build/ovs_fc_topology/fc_agent /app/fc_agent \
    && cp /build/ovs_fc_topology/fc_topology_center /app/fc_topology_center \
    && chmod +x /app/fc_agent /app/fc_topology_center

WORKDIR /app
CMD ["/bin/bash"]
