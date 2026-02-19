# RV-P4 Design Constraints (SDC 2.0)
# Sky130 / OpenROAD compatible
# Target: 1.6GHz datapath, 1.5GHz CPU, 200MHz ctrl, 390MHz MAC

# ============================================================
# Clock Definitions
# ============================================================

# Primary datapath clock: 1.6 GHz
create_clock -name clk_dp \
             -period 0.625 \
             -waveform {0 0.3125} \
             [get_ports clk_dp]

# CPU clock: 1.5 GHz
create_clock -name clk_cpu \
             -period 0.667 \
             -waveform {0 0.333} \
             [get_ports clk_cpu]

# Control plane: 200 MHz
create_clock -name clk_ctrl \
             -period 5.0 \
             -waveform {0 2.5} \
             [get_ports clk_ctrl]

# MAC/PCS clock: 390.625 MHz
create_clock -name clk_mac \
             -period 2.56 \
             -waveform {0 1.28} \
             [get_ports clk_mac]

# PCIe clock
create_clock -name pcie_clk \
             -period 4.0 \
             -waveform {0 2.0} \
             [get_ports pcie_clk]

# JTAG: set slow rate
create_clock -name tck \
             -period 100.0 \
             -waveform {0 50.0} \
             [get_ports tck]

# ============================================================
# Clock Groups & Asynchronous Domains
# ============================================================

# All clocks are asynchronous to each other (different PLLs)
set_clock_groups -asynchronous \
    -group {clk_dp} \
    -group {clk_cpu} \
    -group {clk_ctrl} \
    -group {clk_mac} \
    -group {pcie_clk} \
    -group {tck}

# ============================================================
# Clock Uncertainty (Setup & Hold)
# ============================================================

# Datapath: tightest constraints
set_clock_uncertainty -setup 0.05 [get_clocks clk_dp]
set_clock_uncertainty -hold  0.02 [get_clocks clk_dp]

# CPU clock
set_clock_uncertainty -setup 0.05 [get_clocks clk_cpu]
set_clock_uncertainty -hold  0.02 [get_clocks clk_cpu]

# Control/MAC: relaxed
set_clock_uncertainty -setup 0.10 [get_clocks clk_ctrl]
set_clock_uncertainty -hold  0.05 [get_clocks clk_ctrl]
set_clock_uncertainty -setup 0.08 [get_clocks clk_mac]
set_clock_uncertainty -hold  0.04 [get_clocks clk_mac]

# ============================================================
# Input Delays (relative to clk_dp)
# ============================================================

# SerDes RX inputs (assumed arriving after capture edge)
set_input_delay -clock clk_mac -max 1.0 \
    [get_ports {rx_valid rx_sof rx_eof rx_eop_len rx_data}]
set_input_delay -clock clk_mac -min 0.1 \
    [get_ports {rx_valid rx_sof rx_eof rx_eop_len rx_data}]

# MAC TX flow control
set_input_delay -clock clk_mac -max 0.5 \
    [get_ports {tx_ready}]
set_input_delay -clock clk_mac -min 0.1 \
    [get_ports {tx_ready}]

# PCIe RX data
set_input_delay -clock pcie_clk -max 1.5 \
    [get_ports {pcie_rx_data pcie_rx_valid}]
set_input_delay -clock pcie_clk -min 0.1 \
    [get_ports {pcie_rx_data pcie_rx_valid}]

# JTAG inputs
set_input_delay -clock tck -max 5.0 \
    [get_ports {tms tdi}]

# Async reset (false path)
set_false_path -from [get_ports rst_n]

# Test backdoor ports
set_false_path -from [get_ports {tb_parser_wr_en tb_parser_wr_addr tb_parser_wr_data}]
set_false_path -from [get_ports {tb_tue_apb_*}]

# ============================================================
# Output Delays (relative to clk_dp)
# ============================================================

# SerDes TX outputs
set_output_delay -clock clk_mac -max 1.0 \
    [get_ports {tx_valid tx_sof tx_eof tx_eop_len tx_data}]
set_output_delay -clock clk_mac -min 0.1 \
    [get_ports {tx_valid tx_sof tx_eof tx_eop_len tx_data}]

# MAC RX backpressure
set_output_delay -clock clk_mac -max 0.5 \
    [get_ports {rx_ready}]

# PCIe TX data
set_output_delay -clock pcie_clk -max 1.5 \
    [get_ports {pcie_tx_data pcie_tx_valid}]

# JTAG output
set_output_delay -clock tck -max 5.0 \
    [get_ports {tdo}]

# ============================================================
# Timing Exceptions
# ============================================================

# Multi-cycle paths: SRAM read paths (2-cycle path)
set_multicycle_path 2 -setup -from [get_cells *pkt_buffer*] -to [get_cells *pkt_buffer*]
set_multicycle_path 1 -hold  -from [get_cells *pkt_buffer*] -to [get_cells *pkt_buffer*]

# MAU hash computation (2 cycles)
set_multicycle_path 2 -setup -from [get_cells *mau_hash*] -to [get_cells *mau_hash*]
set_multicycle_path 1 -hold  -from [get_cells *mau_hash*] -to [get_cells *mau_hash*]

# Clock domain crossing: false paths for known async interfaces
# (actual CDC is handled in RTL with synchronizers)
set_false_path -from [get_clocks clk_dp] -to [get_clocks clk_ctrl]
set_false_path -from [get_clocks clk_ctrl] -to [get_clocks clk_dp]
set_false_path -from [get_clocks clk_dp] -to [get_clocks clk_cpu]
set_false_path -from [get_clocks clk_cpu] -to [get_clocks clk_dp]

# ============================================================
# Design Rules
# ============================================================

# Maximum fanout
set_max_fanout 50 [current_design]

# Maximum transition time (in ns)
set_max_transition 0.1 [get_clocks clk_dp]
set_max_transition 0.2 [get_clocks clk_cpu]
set_max_transition 0.5 [get_clocks clk_ctrl]

# Maximum capacitance per net (in pF)
set_max_capacitance 0.5 [current_design]

# ============================================================
# Operating Conditions (Sky130)
# ============================================================

# Typical-Typical corner: 27°C, 1.8V
set_operating_conditions -process 1.0 -voltage 1.8 -temperature 27

# ============================================================
# Load Models
# ============================================================

# Output ports: use standard 0.05pF load (Sky130 CMOS I/O)
set_load 0.05 [all_outputs]

# Input drive: use default drive (standard input drive)
set_driving_cell -lib_cell sky130_fd_sc_hd__buf_1 [all_inputs]
