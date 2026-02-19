#!/bin/bash
# Complete ASIC toolchain installation for RV-P4
# Installs: Yosys, OpenROAD, OpenSTA, Magic, KLayout, Netgen, Sky130 PDK
# Tested on Ubuntu 20.04/22.04

set -e

INSTALL_DIR="${HOME}/tools"
PDK_ROOT="${HOME}/pdk"
LOG_FILE="/tmp/asic_install.log"

# ============================================================
# Utility functions
# ============================================================

log() { echo "[$(date +%H:%M:%S)] $1" | tee -a "${LOG_FILE}"; }
success() { log "✓ $1"; }
info() { log "  $1"; }
error() { log "✗ $1"; exit 1; }

check_tool() {
    if command -v "$1" &> /dev/null; then
        info "$1 already installed: $(command -v $1)"
        return 0
    fi
    return 1
}

# ============================================================
# Check OS
# ============================================================

detect_os() {
    if [ -f /etc/ubuntu-release ] || grep -qi ubuntu /etc/os-release 2>/dev/null; then
        echo "ubuntu"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    else
        echo "unknown"
    fi
}

OS=$(detect_os)
log "Detected OS: $OS"
log "Install prefix: $INSTALL_DIR"
log "PDK root: $PDK_ROOT"

# ============================================================
# System dependencies
# ============================================================

log "Installing system dependencies..."
if [ "$EUID" -ne 0 ]; then
    SUDO="sudo"
else
    SUDO=""
fi

$SUDO apt-get update -qq
$SUDO apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 python3-pip python3-dev \
    tcl-dev \
    libedit-dev \
    libssl-dev \
    flex bison \
    libreadline-dev \
    pkg-config \
    gperf \
    wget curl \
    unzip \
    libglu1-mesa-dev \
    libxrender-dev \
    libboost-all-dev \
    swig \
    qt5-default \
    libqt5svg5-dev \
    libqt5opengl5-dev 2>&1 | tee -a "${LOG_FILE}"

success "System dependencies installed"

# Python packages
pip3 install -q setuptools wheel matplotlib numpy scipy 2>&1 | tee -a "${LOG_FILE}"
success "Python packages installed"

# ============================================================
# Yosys - Logic Synthesis
# ============================================================

install_yosys() {
    log "Installing Yosys..."

    if check_tool yosys; then
        return
    fi

    # Try apt first (fastest)
    if $SUDO apt-get install -y yosys 2>/dev/null; then
        success "Yosys installed via apt"
        return
    fi

    # Build from source
    info "Building Yosys from source..."
    mkdir -p "${INSTALL_DIR}"
    cd "${INSTALL_DIR}"

    if [ ! -d yosys ]; then
        git clone https://github.com/YosysHQ/yosys.git --depth 1
    fi
    cd yosys
    make config-gcc
    make -j$(nproc)
    $SUDO make install

    success "Yosys built and installed"
}

# ============================================================
# OpenROAD - P&R
# ============================================================

install_openroad() {
    log "Installing OpenROAD..."

    if check_tool openroad; then
        return
    fi

    # Try pre-built binary first
    OPENROAD_VERSION="v3.0"
    OPENROAD_URL="https://github.com/The-OpenROAD-Project/OpenROAD/releases/latest/download"

    if $SUDO apt-get install -y openroad 2>/dev/null; then
        success "OpenROAD installed via apt"
        return
    fi

    # Build from source (takes 20-30 minutes)
    info "Building OpenROAD from source (this takes 20-30 minutes)..."
    cd "${INSTALL_DIR}"

    if [ ! -d OpenROAD ]; then
        git clone --recursive https://github.com/The-OpenROAD-Project/OpenROAD.git --depth 1
    fi
    cd OpenROAD

    # Use their install script
    if [ -f etc/DependencyInstaller.sh ]; then
        $SUDO bash etc/DependencyInstaller.sh
    fi

    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j$(nproc)
    $SUDO make install

    success "OpenROAD built and installed"
}

# ============================================================
# OpenSTA - Static Timing Analysis
# ============================================================

install_opensta() {
    log "Installing OpenSTA..."

    if check_tool opensta; then
        return
    fi

    if $SUDO apt-get install -y opensta 2>/dev/null; then
        success "OpenSTA installed via apt"
        return
    fi

    info "Building OpenSTA from source..."
    cd "${INSTALL_DIR}"

    if [ ! -d OpenSTA ]; then
        git clone https://github.com/The-OpenROAD-Project/OpenSTA.git --depth 1
    fi
    cd OpenSTA

    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j$(nproc)
    $SUDO make install

    success "OpenSTA built and installed"
}

# ============================================================
# Magic - Layout Editor & DRC
# ============================================================

install_magic() {
    log "Installing Magic..."

    if check_tool magic; then
        return
    fi

    if $SUDO apt-get install -y magic 2>/dev/null; then
        success "Magic installed via apt"
        return
    fi

    info "Building Magic from source..."
    cd "${INSTALL_DIR}"

    if [ ! -d magic ]; then
        git clone https://github.com/RTimothyEdwards/magic.git --depth 1
    fi
    cd magic
    ./configure
    make -j$(nproc)
    $SUDO make install

    success "Magic built and installed"
}

# ============================================================
# KLayout - GDS Viewer & DRC/LVS
# ============================================================

install_klayout() {
    log "Installing KLayout..."

    if check_tool klayout; then
        return
    fi

    # Try apt first
    if $SUDO apt-get install -y klayout 2>/dev/null; then
        success "KLayout installed via apt"
        return
    fi

    # Try .deb package (Ubuntu)
    info "Downloading KLayout deb package..."
    cd /tmp
    KLAYOUT_VERSION="0.29.1"
    wget -q "https://www.klayout.de/downloads/klayout_${KLAYOUT_VERSION}-1_amd64.deb"
    $SUDO dpkg -i "klayout_${KLAYOUT_VERSION}-1_amd64.deb" || \
        $SUDO apt-get install -f -y

    success "KLayout installed"
}

# ============================================================
# Netgen - LVS
# ============================================================

install_netgen() {
    log "Installing Netgen..."

    if check_tool netgen; then
        return
    fi

    if $SUDO apt-get install -y netgen 2>/dev/null; then
        success "Netgen installed via apt"
        return
    fi

    info "Building Netgen from source..."
    cd "${INSTALL_DIR}"
    if [ ! -d netgen ]; then
        git clone https://github.com/RTimothyEdwards/netgen.git --depth 1
    fi
    cd netgen
    ./configure
    make -j$(nproc)
    $SUDO make install

    success "Netgen built and installed"
}

# ============================================================
# Sky130 PDK Installation
# ============================================================

install_sky130_pdk() {
    log "Installing Sky130 PDK..."

    mkdir -p "${PDK_ROOT}"

    if [ -d "${PDK_ROOT}/sky130A" ]; then
        info "Sky130A PDK already installed"
        return
    fi

    # Method 1: Use open_pdks (most complete)
    info "Installing via open_pdks..."
    cd "${INSTALL_DIR}"

    if [ ! -d open_pdks ]; then
        git clone https://github.com/RTimothyEdwards/open_pdks.git --depth 1
    fi
    cd open_pdks

    ./configure \
        --enable-sky130-pdk \
        --with-sky130-local-path="${PDK_ROOT}" \
        --prefix="${PDK_ROOT}"

    make
    $SUDO make install

    success "Sky130 PDK installed at ${PDK_ROOT}"
}

# ============================================================
# OpenLane - Alternative complete flow
# ============================================================

install_openlane() {
    log "Installing OpenLane (optional - complete automated flow)..."

    if command -v docker &> /dev/null; then
        info "Using Docker-based OpenLane..."

        mkdir -p "${INSTALL_DIR}/openlane"
        cd "${INSTALL_DIR}/openlane"

        if [ ! -f Makefile ]; then
            git clone https://github.com/The-OpenROAD-Project/OpenLane.git . --depth 1
        fi

        # Pull OpenLane Docker image
        make pull-openlane

        success "OpenLane installed (Docker)"
    else
        info "Docker not found, skipping OpenLane"
        info "Install Docker to use OpenLane: https://docs.docker.com/get-docker/"
    fi
}

# ============================================================
# Main installation sequence
# ============================================================

main() {
    log "=========================================="
    log "ASIC Toolchain Installation for RV-P4"
    log "=========================================="
    log "This will install:"
    log "  1. Yosys (logic synthesis)"
    log "  2. OpenROAD (placement & routing)"
    log "  3. OpenSTA (timing analysis)"
    log "  4. Magic (DRC/LVS/GDS)"
    log "  5. KLayout (DRC/GDS viewer)"
    log "  6. Netgen (LVS)"
    log "  7. Sky130 PDK"
    log "  8. OpenLane (automated flow)"
    log ""

    mkdir -p "${INSTALL_DIR}" "${PDK_ROOT}"

    install_yosys
    install_openroad
    install_opensta
    install_magic
    install_klayout
    install_netgen
    install_sky130_pdk
    install_openlane

    # ============================================================
    # Verification
    # ============================================================

    log ""
    log "=========================================="
    log "Installation Verification"
    log "=========================================="

    tools=("yosys" "openroad" "opensta" "magic" "klayout" "netgen")
    all_good=true

    for tool in "${tools[@]}"; do
        if command -v "${tool}" &> /dev/null; then
            success "${tool}: $(${tool} -version 2>&1 | head -1)"
        else
            log "  ✗ ${tool}: NOT FOUND"
            all_good=false
        fi
    done

    if [ "${all_good}" = true ]; then
        log ""
        success "All tools installed successfully!"
    else
        log ""
        log "Some tools not found. Run with sudo or check installation"
    fi

    # ============================================================
    # Environment setup
    # ============================================================

    cat >> "${HOME}/.bashrc" << 'BASHRC'

# ASIC Tools (added by rv_p4 setup)
export PDK_ROOT="${HOME}/pdk"
export TOOLS_ROOT="${HOME}/tools"
export OPENLANE_ROOT="${HOME}/tools/openlane"
export PATH="${TOOLS_ROOT}/yosys:${TOOLS_ROOT}/OpenROAD/build/src:${PATH}"
export PDK=sky130A
BASHRC

    source "${HOME}/.bashrc"

    log ""
    log "=========================================="
    log "Next Steps"
    log "=========================================="
    log "1. Start synthesis:"
    log "   make -f Makefile.asic synth"
    log ""
    log "2. Run full design flow:"
    log "   make -f Makefile.asic all"
    log ""
    log "3. Generate GDS only:"
    log "   make -f Makefile.asic gds"
    log ""
    log "See Makefile.asic for all available targets"
}

main
