FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PDK_ROOT=/root/pdk
ENV TOOLS_ROOT=/root/tools

# Install system dependencies
RUN apt-get update -qq && apt-get install -y \
    build-essential cmake git \
    python3 python3-pip python3-dev \
    tcl-dev libedit-dev libssl-dev \
    flex bison libreadline-dev \
    pkg-config gperf wget curl unzip \
    libglu1-mesa-dev libxrender-dev \
    libboost-all-dev swig \
    qt5-default libqt5svg5-dev libqt5opengl5-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Python packages
RUN pip3 install -q setuptools wheel matplotlib numpy scipy

# Install Yosys
RUN git clone https://github.com/YosysHQ/yosys.git /tmp/yosys --depth 1 && \
    cd /tmp/yosys && \
    make config-gcc && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/yosys

# Install OpenROAD (pre-built if available, else skip for now)
RUN apt-get update && apt-get install -y openroad 2>/dev/null || true

# Install OpenSTA
RUN git clone https://github.com/The-OpenROAD-Project/OpenSTA.git /tmp/opensta --depth 1 && \
    cd /tmp/opensta && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install && \
    cd / && rm -rf /tmp/opensta

# Install Magic
RUN git clone https://github.com/RTimothyEdwards/magic.git /tmp/magic --depth 1 && \
    cd /tmp/magic && \
    ./configure && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/magic

# Install Netgen
RUN git clone https://github.com/RTimothyEdwards/netgen.git /tmp/netgen --depth 1 && \
    cd /tmp/netgen && \
    ./configure && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/netgen

# Install KLayout
RUN apt-get update && apt-get install -y klayout 2>/dev/null || true

# Install Sky130 PDK
RUN mkdir -p ${PDK_ROOT} && \
    git clone https://github.com/google/skywater-pdk.git ${PDK_ROOT}/sky130 --depth 1

# Create working directory
RUN mkdir -p /work
WORKDIR /work

# Setup environment
RUN echo 'export PDK_ROOT=/root/pdk' >> /root/.bashrc && \
    echo 'export TOOLS_ROOT=/root/tools' >> /root/.bashrc && \
    echo 'export PATH="/usr/local/bin:$PATH"' >> /root/.bashrc

ENTRYPOINT ["/bin/bash"]
