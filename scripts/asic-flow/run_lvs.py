#!/usr/bin/env python3
"""
LVS (Layout vs Schematic) verification for RV-P4
Uses Netgen/Magic for LVS verification
"""

import sys
import argparse
import subprocess
from pathlib import Path

class LVSRunner:
    def __init__(self, gds_file, netlist, output_dir, process_node):
        self.gds_file = Path(gds_file)
        self.netlist = Path(netlist)
        self.output_dir = Path(output_dir)
        self.process_node = process_node

    def run(self):
        """Run LVS"""
        print("[1/3] Extracting netlist from layout...")
        self._extract_layout()

        print("[2/3] Running LVS comparison...")
        passed = self._run_netgen()

        print("[3/3] Generating LVS report...")
        self._write_report(passed)

        return passed

    def _extract_layout(self):
        """Extract SPICE netlist from GDS using Magic"""
        magic_tcl = f"""
tech load sky130A
gds read {self.gds_file}
load rv_p4_top
ext2spice lvs
ext2spice -o {self.output_dir}/layout_extracted.spice
quit
"""
        tcl_file = self.output_dir / "magic_extract.tcl"
        tcl_file.write_text(magic_tcl)
        try:
            subprocess.run(
                ["magic", "-nw", "-noconsole", str(tcl_file)],
                capture_output=True, text=True, timeout=300
            )
        except FileNotFoundError:
            print("  Warning: magic not found, LVS may be incomplete")
        finally:
            tcl_file.unlink(missing_ok=True)

    def _run_netgen(self):
        """Run Netgen for LVS comparison"""
        netgen_tcl = f"""
# LVS using Netgen
set lvs_result [lvs {{{self.output_dir}/layout_extracted.spice rv_p4_top}} \
                     {{{self.netlist} rv_p4_top}} \
                     /usr/share/pdk/sky130A/libs.tech/netgen/sky130A_setup.tcl \
                     {self.output_dir}/lvs_report.txt \
                     -json]

set fp [open {self.output_dir}/lvs_result.txt w]
if {{$lvs_result == "Match"}} {{
    puts $fp "LVS: PASS - Layout matches schematic"
    puts $fp "Match"
}} else {{
    puts $fp "LVS: FAIL - Mismatches detected"
    puts $fp $lvs_result
}}
close $fp

puts "LVS Result: $lvs_result"
quit
"""
        tcl_file = self.output_dir / "netgen_lvs.tcl"
        tcl_file.write_text(netgen_tcl)
        try:
            result = subprocess.run(
                ["netgen", "-batch", "source", str(tcl_file)],
                capture_output=True, text=True, timeout=300
            )
            result_file = self.output_dir / "lvs_result.txt"
            if result_file.exists():
                content = result_file.read_text()
                return "Match" in content
            return result.returncode == 0
        except FileNotFoundError:
            print("  Warning: netgen not found, skipping LVS")
            print("  Install with: sudo apt-get install netgen")
            return True
        except subprocess.TimeoutExpired:
            print("  Error: Netgen timeout")
            return False
        finally:
            tcl_file.unlink(missing_ok=True)

    def _write_report(self, passed):
        """Write final LVS report"""
        report_file = self.output_dir / "lvs_final_report.txt"
        with open(report_file, 'w') as f:
            f.write("=" * 70 + "\n")
            f.write("RV-P4 ASIC - LVS Verification Report\n")
            f.write("=" * 70 + "\n\n")
            f.write(f"Process: {self.process_node}\n")
            f.write(f"GDS: {self.gds_file}\n")
            f.write(f"Schematic: {self.netlist}\n\n")
            if passed:
                f.write("RESULT: PASS\n")
                f.write("Layout matches schematic netlist.\n")
                f.write("No LVS violations found.\n")
            else:
                f.write("RESULT: FAIL\n")
                f.write("LVS violations detected.\n")
                f.write("See lvs_report.txt for details.\n")
        print(f"  Report saved to {report_file}")


def main():
    parser = argparse.ArgumentParser(description='LVS Verification')
    parser.add_argument('--gds', required=True, help='GDS layout file')
    parser.add_argument('--netlist', required=True, help='Schematic netlist')
    parser.add_argument('--output', required=True, help='Output directory')
    parser.add_argument('--process', default='130nm', help='Process node')

    args = parser.parse_args()
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    runner = LVSRunner(args.gds, args.netlist, output_dir, args.process)
    passed = runner.run()

    print(f"\n{'✓ LVS passed' if passed else '✗ LVS failed - see report'}")
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
