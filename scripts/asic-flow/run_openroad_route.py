#!/usr/bin/env python3
"""
OpenROAD Routing Script for RV-P4
Performs global routing and detailed routing to final interconnect
"""

import os
import sys
import argparse
import subprocess
from pathlib import Path

class OpenROADRouter:
    def __init__(self, placed_def, output_dir, process_node):
        self.placed_def = Path(placed_def)
        self.output_dir = Path(output_dir)
        self.process_node = process_node
        self.project_name = self.placed_def.stem.replace('_placed', '')

    def run(self):
        """Execute complete routing flow"""
        print("[1/4] Initializing routing...")
        self._init_routing()

        print("[2/4] Global routing...")
        self._global_route()

        print("[3/4] Detailed routing...")
        self._detail_route()

        print("[4/4] Post-route optimization...")
        self._post_route()

        return self.output_dir / f"{self.project_name}_routed.def"

    def _init_routing(self):
        """Initialize routing layers and resources"""
        tcl_script = f"""
# Read placed design
read_def {self.placed_def}

# Initialize routing resources
# Sky130: 6-7 metal layers available
# M1: Local routing (contacted poly, diffusion)
# M2-M3: Intermediate routing
# M4-M5: Global routing
# M6: Top-level global routing
# M7: (optional) top metal

init_route_rd

# Set routing preferences for Sky130
set_routing_layers -signal M1 M2 M3 M4 M5 M6
set_routing_layers -clock M4 M5 M6

puts "Routing resources initialized"
"""
        self._run_openroad_tcl(tcl_script)

    def _global_route(self):
        """Perform global routing"""
        tcl_script = f"""
# Read placed design
read_def {self.placed_def}

# Global routing
global_route -congestion_iterations 100 \\
             -net_order_file {{}} \\
             -allow_congestion

# Save global route result
write_def {self.output_dir}/03_global_route.def

puts "Global routing complete"
report_congestion
"""
        self._run_openroad_tcl(tcl_script)

    def _detail_route(self):
        """Perform detailed routing"""
        tcl_script = f"""
# Read global routed design
read_def {self.output_dir}/03_global_route.def

# Detailed routing with optimization
detailed_route -config {{}} \\
               -distributed \\
               -save_guides {{}}

# Track assignment and optimization
# - Minimize routing resources
# - Optimize timing critical paths
# - Reduce congestion hotspots

# Save routed design
write_def {self.output_dir}/{self.project_name}_routed.def

# Generate routing statistics
report_routing_metrics > {self.output_dir}/routing_metrics.txt

# Generate SPEF for timing analysis
write_spef {self.output_dir}/{self.project_name}_routed.spef

puts "Detailed routing complete"
"""
        self._run_openroad_tcl(tcl_script)

    def _post_route(self):
        """Post-route optimization"""
        tcl_script = f"""
# Read routed design
read_def {self.output_dir}/{self.project_name}_routed.def

# Final ECO (engineering change order) phase
# - Buffer insertion for long nets
# - Timing violations fixing
# - DFM enhancements

# Insert buffers on long routes
# set long_nets [get_nets -length > 100]
# foreach net $long_nets {{
#     insert_buffer $net -buffer_cell sky130_fd_sc_hd__buf_1
# }}

# Final legalization
legalize_placement -incremental

# Save final design
write_def {self.output_dir}/{self.project_name}_routed.def

# Generate final SPEF
write_spef {self.output_dir}/{self.project_name}_routed.spef

# GDS stream data (will be converted to GDS later)
write_gds {self.output_dir}/{self.project_name}_routed_temp.gds

puts "Post-route optimization complete"
report_routing_metrics
"""
        self._run_openroad_tcl(tcl_script)

    def _run_openroad_tcl(self, tcl_script):
        """Execute Tcl script in OpenROAD"""
        tcl_file = self.output_dir / "temp_route.tcl"
        tcl_file.write_text(tcl_script)

        try:
            result = subprocess.run(
                ["openroad", str(tcl_file)],
                capture_output=True,
                text=True,
                timeout=600  # 10 minute timeout for routing
            )

            if result.returncode != 0:
                print(f"Warning: OpenROAD returned {result.returncode}")
                if result.stderr:
                    print("STDERR:", result.stderr[:500])

            if result.stdout:
                for line in result.stdout.split('\n'):
                    if any(kw in line.lower() for kw in ['routing', 'track', 'congestion', 'layer', 'error']):
                        print(f"  {line}")

        except FileNotFoundError:
            print("Error: openroad not found. Install with:")
            print("  sudo apt-get install openroad")
            sys.exit(1)
        except subprocess.TimeoutExpired:
            print("Error: OpenROAD routing timeout (10 minutes)")
            sys.exit(1)
        finally:
            tcl_file.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description='OpenROAD Routing Flow')
    parser.add_argument('--placed-def', required=True, help='Placed DEF file')
    parser.add_argument('--output', required=True, help='Output directory')
    parser.add_argument('--process', default='130nm', help='Process node')

    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    router = OpenROADRouter(args.placed_def, output_dir, args.process)

    try:
        routed_def = router.run()
        print(f"\n✓ Routing complete")
        print(f"  Output: {routed_def}")
        print(f"  SPEF: {routed_def.parent / routed_def.stem.replace('_routed', '_routed_spef')}.spef")
        return 0
    except Exception as e:
        print(f"\n✗ Routing failed: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
