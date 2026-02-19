# Yosys Synthesis Script for RV-P4 Area Evaluation
# Using Sky130 PDK
# Run with: yosys -m ghdl synthesis_area_eval.tcl

# Read design files
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

# Set top module
hierarchy -top rv_p4_top

# Flatten design (optional - for area estimation only)
flatten

# Perform synthesis
synth_sky130 -json area_report.json -abc9 -nobram

# Generate reports
stat
tee -o synth_area.txt stat -width

# Show final netlist stats
show -format dot -prefix rv_p4_final

# Exit with success
exit 0
