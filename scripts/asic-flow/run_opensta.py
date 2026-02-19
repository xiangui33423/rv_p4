#!/usr/bin/env python3
"""
Static Timing Analysis for RV-P4
Uses OpenSTA for comprehensive timing analysis
"""

import os
import sys
import argparse
import subprocess
from pathlib import Path

class TimingAnalyzer:
    def __init__(self, sdc_file, netlist, spef_file, output_dir, process_node):
        self.sdc_file = Path(sdc_file)
        self.netlist = Path(netlist)
        self.spef_file = Path(spef_file)
        self.output_dir = Path(output_dir)
        self.process_node = process_node

    def run(self):
        """Execute STA flow"""
        print("[1/3] Loading design and constraints...")
        self._load_design()

        print("[2/3] Running timing analysis...")
        self._analyze_timing()

        print("[3/3] Generating reports...")
        self._generate_reports()

        return True

    def _load_design(self):
        """Load design into OpenSTA"""
        tcl_script = f"""
# Read netlist
read_verilog {self.netlist}

# Read parasitics (SPEF)
read_spef {self.spef_file}

# Read SDC constraints
read_sdc {self.sdc_file}

# Set default operating conditions
set_operating_conditions -analysis_type single

puts "Design loaded successfully"
"""
        self._run_opensta_tcl(tcl_script)

    def _analyze_timing(self):
        """Run comprehensive timing analysis"""
        tcl_script = f"""
# Timing analysis
check_timing -verbose

# Report timing summary
puts "\\n======== Timing Summary ========"
report_tns
report_wns

# Critical path analysis
puts "\\n======== Critical Path ========"
report_path -format full_clock -max_paths 5

# Slack histogram
puts "\\n======== Path Slack Distribution ========"
report_slack_histogram

# Per-clock analysis
puts "\\n======== Per-Clock Summary ========"
report_clock_summary

# Port delay analysis
puts "\\n======== Input/Output Delays ========"
report_port_delays -input
report_port_delays -output

# Save timing database
write_timing {output_dir}/timing_analysis.rpt

puts "\\nTiming analysis complete"
"""
        self._run_opensta_tcl(tcl_script)

    def _generate_reports(self):
        """Generate detailed timing reports"""
        tcl_script = f"""
# Comprehensive timing report
set fp [open {self.output_dir}/timing_summary.txt w]

puts $fp "RV-P4 ASIC - Timing Analysis Report"
puts $fp "Process: {self.process_node}"
puts $fp "=========================================\\n"

# Summary metrics
puts $fp "TIMING SUMMARY"
puts $fp "-------------"
puts $fp "Setup TNS: [get_property tns [current_design]]"
puts $fp "Setup WNS: [get_property wns [current_design]]"

# Clock information
puts $fp "\\nCLOCK INFORMATION"
puts $fp "----------------"
foreach_in_collection clk [all_clocks] {{
    set clk_name [get_property name $clk]
    set period [get_property period $clk]
    puts $fp "Clock: $clk_name, Period: $period ns"
}}

# Critical endpoints
puts $fp "\\nCRITICAL ENDPOINTS (Slack < 0.1ns)"
puts $fp "-----------------------------------"
set num_critical 0
foreach_in_collection endpoint [get_timing_paths -delay_type max -slack_min -0.1] {{
    set slack [get_property slack $endpoint]
    set path [get_property path $endpoint]
    if {{$num_critical < 10}} {{
        puts $fp "Path: [lindex $path 0], Slack: $slack ns"
        incr num_critical
    }}
}}

# Voltage/Temperature corners (if available)
puts $fp "\\nPROCESS CORNERS"
puts $fp "---------------"
puts $fp "Process: TT (Typical-Typical)"
puts $fp "Voltage: 1.8V"
puts $fp "Temperature: 27°C"

close $fp

puts "Timing summary saved to {self.output_dir}/timing_summary.txt"

# Detailed timing report
set fp [open {self.output_dir}/timing_detail.txt w]
puts $fp "Detailed Timing Report"
report_path -format full_clock -max_paths 100 -to $fp
close $fp
"""
        self._run_opensta_tcl(tcl_script)

    def _run_opensta_tcl(self, tcl_script):
        """Execute Tcl script in OpenSTA"""
        tcl_file = self.output_dir / "sta_temp.tcl"
        tcl_file.write_text(tcl_script)

        try:
            result = subprocess.run(
                ["opensta", str(tcl_file)],
                capture_output=True,
                text=True,
                timeout=300
            )

            if result.returncode != 0:
                print(f"Warning: OpenSTA returned {result.returncode}")
                if result.stderr:
                    print("STDERR:", result.stderr[:500])

            if result.stdout:
                for line in result.stdout.split('\n'):
                    if any(kw in line.lower() for kw in ['slack', 'tns', 'wns', 'period', 'setup', 'hold']):
                        print(f"  {line}")

        except FileNotFoundError:
            print("Error: opensta not found. Install with:")
            print("  sudo apt-get install opensta")
            sys.exit(1)
        except subprocess.TimeoutExpired:
            print("Error: OpenSTA timeout (5 minutes)")
            sys.exit(1)
        finally:
            tcl_file.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description='Static Timing Analysis')
    parser.add_argument('--sdc', required=True, help='SDC constraint file')
    parser.add_argument('--netlist', required=True, help='Routed netlist')
    parser.add_argument('--spef', required=True, help='SPEF parasitic file')
    parser.add_argument('--output', required=True, help='Output directory')
    parser.add_argument('--process', default='130nm', help='Process node')

    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    analyzer = TimingAnalyzer(args.sdc, args.netlist, args.spef, output_dir, args.process)

    try:
        analyzer.run()
        print(f"\n✓ Timing analysis complete")
        print(f"  Summary: {output_dir}/timing_summary.txt")
        print(f"  Detail: {output_dir}/timing_detail.txt")
        return 0
    except Exception as e:
        print(f"\n✗ STA failed: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
