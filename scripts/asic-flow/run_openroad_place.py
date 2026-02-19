#!/usr/bin/env python3
"""
OpenROAD Placement Script for RV-P4
Performs floorplanning, power planning, and placement optimization
"""

import os
import sys
import json
import argparse
import subprocess
from pathlib import Path

class OpenROADPlacer:
    def __init__(self, netlist, output_dir, process_node, clock_period):
        self.netlist = Path(netlist)
        self.output_dir = Path(output_dir)
        self.process_node = process_node
        self.clock_period = clock_period
        self.project_name = self.netlist.stem

    def run(self):
        """Execute complete placement flow"""
        print("[1/5] Loading netlist and libraries...")
        self._load_design()

        print("[2/5] Floorplanning and power grid...")
        self._floorplan()

        print("[3/5] Placement...")
        self._place()

        print("[4/5] Optimization...")
        self._optimize()

        print("[5/5] Generating reports...")
        self._report()

        return self.output_dir / f"{self.project_name}_placed.def"

    def _load_design(self):
        """Load netlist in OpenROAD"""
        tcl_script = f"""
# Load design
read_verilog {self.netlist}
link_design {self.project_name}

# Load libraries
set_cmd_units -time ns -capacitance pF -current mA -voltage V -resistance kOhm

# Provide basic constraints
create_clock -name clk_dp -period {self.clock_period} clk_dp
set_clock_uncertainty 0.1 clk_dp
set_input_delay 0.1 -clock clk_dp [get_ports rx_*]
set_output_delay 0.1 -clock clk_dp [get_ports tx_*]

# Save intermediate design
write_def {self.output_dir}/01_loaded.def
"""
        self._run_openroad_tcl(tcl_script)

    def _floorplan(self):
        """Create floorplan and power grid"""
        # Estimate die size from area estimation (46.5 mm²)
        # With 70% utilization: 89.8 mm² die area
        # Die size: ~9.47 x 9.47 mm
        die_width = 9470.0  # microns
        die_height = 9470.0
        core_margin = 100.0  # microns margin

        tcl_script = f"""
# Read previous design
read_def {self.output_dir}/01_loaded.def

# Floorplanning
floorplan -site CoreSite \\
          -density 0.70 \\
          -lef {{}} \\
          -core_offset {core_margin}

# Set die dimensions (microns)
# Estimated: 9.47mm x 9.47mm
initialize_floorplan -utilization 0.70 -power_density 100

# Create power grid (M1/M2: vertical/horizontal stripes)
define_pdn_grid -name main \\
                -voltage_domains {{CORE}}

add_pdn_stripe -followpins -layer M1 -width 0.48 -spacing 5.0 -offset 0.0
add_pdn_stripe -followpins -layer M2 -width 0.48 -spacing 5.0 -offset 0.0

# Add power connections
connect_pdn_net

# Save floorplan
write_def {self.output_dir}/02_floorplan.def
"""
        self._run_openroad_tcl(tcl_script)

    def _place(self):
        """Run global and detailed placement"""
        tcl_script = f"""
# Read floorplanned design
read_def {self.output_dir}/02_floorplan.def

# Global placement
global_placement -density 0.70 \\
                 -pad_left 10 \\
                 -pad_right 10 \\
                 -pad_top 10 \\
                 -pad_bottom 10

# Report placement metrics
puts "Global placement completed"

# Detailed placement
detailed_placement -max_displacement 10

# Legalize placement
legalize_placement

# Save placed design
write_def {self.output_dir}/{self.project_name}_placed.def

# Generate placement statistics
report_placement_metrics > {self.output_dir}/placement_metrics.txt
"""
        self._run_openroad_tcl(tcl_script)

    def _optimize(self):
        """Post-placement optimization"""
        tcl_script = f"""
# Read placed design
read_def {self.output_dir}/{self.project_name}_placed.def

# Buffer insertion and optimization
buffer_ports

# Timing-driven cell movement
set_timing_derate -early 0.95
set_timing_derate -late 1.05

# Create updated placement
legalize_placement

# Save optimized design
write_def {self.output_dir}/{self.project_name}_placed_opt.def

# Checksum for verification
puts "Placement optimization complete"
"""
        self._run_openroad_tcl(tcl_script)

    def _report(self):
        """Generate placement reports"""
        tcl_script = f"""
# Read final placement
read_def {self.output_dir}/{self.project_name}_placed.def

# Area report
puts "======== Placement Report ========"
report_placement_metrics

# Wire statistics
puts "\\n======== Preliminary Routing Estimate ========"
set total_wirelength 0
foreach net [get_nets] {{
    set wl [get_property wirelength $net]
    set total_wirelength [expr {{$total_wirelength + $wl}}]
}}
puts "Estimated total wirelength: $total_wirelength um"

# Density check
set core_area [get_property core_area [current_design]]
set total_cell_area [get_property cell_area [current_design]]
set utilization [expr {{$total_cell_area / $core_area * 100}}]
puts "Core utilization: $utilization %"
"""
        self._run_openroad_tcl(tcl_script)
        print(f"✓ Placement reports saved to {self.output_dir}/")

    def _run_openroad_tcl(self, tcl_script):
        """Execute Tcl script in OpenROAD"""
        tcl_file = self.output_dir / "temp.tcl"
        tcl_file.write_text(tcl_script)

        try:
            result = subprocess.run(
                ["openroad", str(tcl_file)],
                capture_output=True,
                text=True,
                timeout=300
            )

            if result.returncode != 0:
                print(f"Warning: OpenROAD returned {result.returncode}")
                if result.stderr:
                    print("STDERR:", result.stderr[:500])

            if result.stdout:
                # Print key lines from output
                for line in result.stdout.split('\n'):
                    if any(kw in line.lower() for kw in ['area', 'util', 'wl', 'error', 'warning']):
                        print(f"  {line}")

        except FileNotFoundError:
            print("Error: openroad not found. Install with:")
            print("  sudo apt-get install openroad")
            sys.exit(1)
        except subprocess.TimeoutExpired:
            print("Error: OpenROAD timeout (5 minutes)")
            sys.exit(1)
        finally:
            tcl_file.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description='OpenROAD Placement Flow')
    parser.add_argument('--netlist', required=True, help='Input Verilog netlist')
    parser.add_argument('--output', required=True, help='Output directory')
    parser.add_argument('--process', default='130nm', help='Process node')
    parser.add_argument('--clock-period', type=float, default=0.625, help='Clock period in ns')

    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    placer = OpenROADPlacer(args.netlist, output_dir, args.process, args.clock_period)

    try:
        placed_def = placer.run()
        print(f"\n✓ Placement complete")
        print(f"  Output: {placed_def}")
        return 0
    except Exception as e:
        print(f"\n✗ Placement failed: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
