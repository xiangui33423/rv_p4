#!/bin/bash
# Setup script for Yosys + Sky130 PDK environment
# Run this before synthesis

set -e

PROJECT_ROOT=$(pwd)
PDK_DIR="${PROJECT_ROOT}/pdk"
SKY130_DIR="${PDK_DIR}/sky130"

echo "======================================"
echo "RV-P4 ASIC Area Evaluation Setup"
echo "======================================"

# Create PDK directory
mkdir -p "${PDK_DIR}"

# Clone Sky130 PDK (open source)
if [ ! -d "${SKY130_DIR}" ]; then
    echo "Downloading Sky130 PDK..."
    git clone https://github.com/google/skywater-pdk.git "${SKY130_DIR}" --depth 1
    echo "✓ Sky130 PDK downloaded"
else
    echo "✓ Sky130 PDK already present"
fi

# Install Yosys (if not present)
if ! command -v yosys &> /dev/null; then
    echo "Installing Yosys..."
    # Option 1: Using apt (Ubuntu/Debian)
    sudo apt-get update
    sudo apt-get install -y yosys yosys-doc
    echo "✓ Yosys installed"
else
    echo "✓ Yosys already installed"
fi

# Install additional open-source tools
if ! command -v opensta &> /dev/null; then
    echo "Installing OpenSTA for timing analysis..."
    sudo apt-get install -y opensta
fi

if ! command -v magic &> /dev/null; then
    echo "Installing Magic for physical design..."
    sudo apt-get install -y magic
fi

echo ""
echo "======================================"
echo "Setup Complete!"
echo "======================================"
echo ""
echo "To run synthesis:"
echo "  cd ${PROJECT_ROOT}"
echo "  ./scripts/run_synthesis.sh"
echo ""
echo "For area estimation only:"
echo "  yosys -m ghdl scripts/synthesis_area_eval.tcl"
echo ""
