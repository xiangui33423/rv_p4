#!/bin/bash
# Comprehensive synthesis script for RV-P4 ASIC area evaluation
# Uses Yosys with Sky130 open-source PDK

set -e

PROJECT_ROOT=$(pwd)
RESULTS_DIR="${PROJECT_ROOT}/synthesis_results"
LOG_DIR="${RESULTS_DIR}/logs"

# Create output directories
mkdir -p "${RESULTS_DIR}" "${LOG_DIR}"

echo "======================================"
echo "RV-P4 ASIC Synthesis & Area Analysis"
echo "======================================"
echo "Project Root: ${PROJECT_ROOT}"
echo "Results Dir: ${RESULTS_DIR}"
echo ""

# Step 1: RTL Lint and Elaboration
echo "[Step 1/4] RTL Elaboration and Type Checking..."
yosys -p "
    read_verilog -sv rtl/include/rv_p4_pkg.sv
    read_verilog -sv rtl/include/rv_p4_if.sv
    read_verilog -sv rtl/parser/parser_tcam.sv
    read_verilog -sv rtl/parser/p4_parser.sv
    read_verilog -sv rtl/deparser/deparser.sv
    read_verilog -sv rtl/mau/mau_alu.sv
    read_verilog -sv rtl/mau/mau_hash.sv
    read_verilog -sv rtl/mau/mau_tcam.sv
    read_verilog -sv rtl/mau/mau_stage.sv
    read_verilog -sv rtl/tue/tue.sv
    read_verilog -sv rtl/tm/traffic_manager.sv
    read_verilog -sv rtl/pkt_buffer/pkt_buffer.sv
    read_verilog -sv rtl/ctrl/ctrl_plane.sv
    read_verilog -sv rtl/top/mac_rx_arb.sv
    read_verilog -sv rtl/top/ctrl_plane.sv
    read_verilog -sv rtl/top/rst_sync.sv
    read_verilog -sv rtl/top/rv_p4_top.sv
    hierarchy -top rv_p4_top
    stat -width
" 2>&1 | tee "${LOG_DIR}/01_elaboration.log"

# Step 2: Synthesis with optimizations
echo "[Step 2/4] Logic Synthesis with Optimizations..."
yosys -p "
    read_verilog -sv rtl/include/rv_p4_pkg.sv
    read_verilog -sv rtl/include/rv_p4_if.sv
    read_verilog -sv rtl/parser/parser_tcam.sv
    read_verilog -sv rtl/parser/p4_parser.sv
    read_verilog -sv rtl/deparser/deparser.sv
    read_verilog -sv rtl/mau/mau_alu.sv
    read_verilog -sv rtl/mau/mau_hash.sv
    read_verilog -sv rtl/mau/mau_tcam.sv
    read_verilog -sv rtl/mau/mau_stage.sv
    read_verilog -sv rtl/tue/tue.sv
    read_verilog -sv rtl/tm/traffic_manager.sv
    read_verilog -sv rtl/pkt_buffer/pkt_buffer.sv
    read_verilog -sv rtl/ctrl/ctrl_plane.sv
    read_verilog -sv rtl/top/mac_rx_arb.sv
    read_verilog -sv rtl/top/ctrl_plane.sv
    read_verilog -sv rtl/top/rst_sync.sv
    read_verilog -sv rtl/top/rv_p4_top.sv

    # Hierarchy and optimization
    hierarchy -top rv_p4_top
    flatten

    # Technology-independent optimizations
    opt -full -undriven -mux_undef
    fsm
    opt -fast
    memory -nomap
    opt -full
    techmap -map +/cmp2lut.v
    opt -full -fast

    # Statistics before technology mapping
    stat -width > ${RESULTS_DIR}/stats_rtl.txt

    # Technology mapping to Sky130 cells (if available)
    # If Sky130 cells are not available, use generic gates for estimation
    abc -exe yosys-abc -script +strash;scorr;ifraig;retime,{D};strash;dch,-f;map,-p;tmap,-p;amap,-p

    # Final optimization
    opt -full
    clean

    # Final statistics
    stat -width > ${RESULTS_DIR}/stats_mapped.txt

    # Write netlist for further analysis
    write_verilog -attr2comment -precision 6 ${RESULTS_DIR}/rv_p4_netlist.v
    write_json ${RESULTS_DIR}/rv_p4_netlist.json

    # Generate graph visualization
    show -format dot -prefix ${RESULTS_DIR}/rv_p4_rtl_graph
" 2>&1 | tee "${LOG_DIR}/02_synthesis.log"

# Step 3: Area and cell analysis
echo "[Step 3/4] Cell Count and Area Analysis..."
python3 << 'PYTHON_SCRIPT'
import json
import re
import sys

results_file = "synthesis_results/rv_p4_netlist.json"
stats_file = "synthesis_results/stats_mapped.txt"

try:
    # Parse JSON netlist
    with open(results_file, 'r') as f:
        netlist = json.load(f)

    # Count cells by type
    cell_counts = {}
    total_cells = 0

    for module_name, module_data in netlist.get('modules', {}).items():
        if 'cells' in module_data:
            for cell_name, cell_data in module_data['cells'].items():
                cell_type = cell_data.get('type', 'unknown')
                cell_counts[cell_type] = cell_counts.get(cell_type, 0) + 1
                total_cells += 1

    # Write area report
    with open("synthesis_results/area_report.txt", 'w') as f:
        f.write("=" * 70 + "\n")
        f.write("RV-P4 ASIC Area Estimation Report\n")
        f.write("=" * 70 + "\n\n")

        f.write(f"Total Cells: {total_cells}\n\n")

        f.write("Cell Type Distribution:\n")
        f.write("-" * 70 + "\n")
        f.write(f"{'Cell Type':<40} {'Count':>15} {'Percentage':>15}\n")
        f.write("-" * 70 + "\n")

        sorted_cells = sorted(cell_counts.items(), key=lambda x: x[1], reverse=True)
        for cell_type, count in sorted_cells:
            percentage = (count / total_cells) * 100 if total_cells > 0 else 0
            f.write(f"{cell_type:<40} {count:>15} {percentage:>14.1f}%\n")

        f.write("-" * 70 + "\n")
        f.write(f"{'TOTAL':<40} {total_cells:>15}\n")
        f.write("=" * 70 + "\n\n")

        # Estimate area (rough approximation)
        # Sky130: ~1-2 μm² per cell average
        # Gate density: ~10-20 million cells per mm²
        avg_area_per_cell = 1.5  # μm²
        total_area = total_cells * avg_area_per_cell

        f.write("Area Estimation (Sky130 Process):\n")
        f.write("-" * 70 + "\n")
        f.write(f"Average Cell Area:        {avg_area_per_cell} μm²\n")
        f.write(f"Estimated Core Area:      {total_area:.0f} μm² ({total_area/1e6:.2f} mm²)\n")
        f.write(f"With 30% overhead (P&R):  {total_area*1.3/1e6:.2f} mm²\n")
        f.write("-" * 70 + "\n")

    print("✓ Area analysis complete. See synthesis_results/area_report.txt")

except Exception as e:
    print(f"✗ Error during analysis: {e}")
    sys.exit(1)

PYTHON_SCRIPT

# Step 4: Summary
echo "[Step 4/4] Generating Summary Report..."
cat > "${RESULTS_DIR}/SYNTHESIS_REPORT.md" << 'EOF'
# RV-P4 ASIC Synthesis Report

## Design Summary
- **Design**: RV-P4 (RISC-V P4 Switch)
- **Process**: Sky130 (Open Source)
- **Synthesis Tool**: Yosys (Open Source)

## Key Results

### Cell Statistics
See `stats_mapped.txt` for detailed cell counts

### Area Estimation
See `area_report.txt` for detailed area breakdown

## Files Generated
- `rv_p4_netlist.v` - Synthesized Verilog netlist
- `rv_p4_netlist.json` - Netlist in JSON format
- `stats_rtl.txt` - RTL-level statistics
- `stats_mapped.txt` - Mapped statistics
- `area_report.txt` - Detailed area analysis
- `rv_p4_rtl_graph.dot` - RTL structure graph

## Next Steps
1. **Place & Route**: Use OpenROAD or commercial tools for P&R
2. **Timing Analysis**: Run OpenSTA for timing closure
3. **Power Analysis**: Estimate power consumption
4. **Design Optimization**: Based on results, optimize hot paths

## Notes
- This is an estimation based on cell counts
- Actual area depends on place & route and cell library selection
- Buffer and repeater insertion during P&R will increase area by 20-40%
EOF

echo ""
echo "======================================"
echo "Synthesis Complete!"
echo "======================================"
echo ""
echo "Results saved in: ${RESULTS_DIR}/"
echo "Logs saved in: ${LOG_DIR}/"
echo ""
echo "View summary: cat ${RESULTS_DIR}/SYNTHESIS_REPORT.md"
echo "View area report: cat ${RESULTS_DIR}/area_report.txt"
echo ""
