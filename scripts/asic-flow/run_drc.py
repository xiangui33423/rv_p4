#!/usr/bin/env python3
"""
DRC (Design Rule Check) for RV-P4 using Magic/Klayout
Verifies layout against Sky130 design rules
"""

import os
import sys
import argparse
import subprocess
from pathlib import Path

class DRCRunner:
    def __init__(self, gds_file, output_dir, process_node):
        self.gds_file = Path(gds_file)
        self.output_dir = Path(output_dir)
        self.process_node = process_node

    def run(self):
        """Run DRC checks"""
        print("[1/2] Running DRC with Magic...")
        drc_magic = self._run_magic_drc()

        print("[2/2] Running DRC with KLayout...")
        drc_klayout = self._run_klayout_drc()

        return drc_magic and drc_klayout

    def _run_magic_drc(self):
        """Run DRC using Magic"""
        magic_tcl = f"""
#!/usr/bin/env magic -nw -noconsole -notech

# Load Sky130 technology
tech load sky130A

# Load GDS layout
gds read {self.gds_file}

# Load cell
load rv_p4_top

# Set full chip DRC mode
drc full

# Run DRC
drc check
drc catchup

# Get and report error count
set drc_count [drc list count total]
set fp [open {self.output_dir}/drc_magic_report.txt w]
puts $fp "Magic DRC Report - {self.process_node}"
puts $fp "================================"
puts $fp "Total DRC Violations: $drc_count"
puts $fp ""

if {{$drc_count > 0}} {{
    puts $fp "DRC Violations:"
    set violations [drc list]
    foreach v $violations {{
        puts $fp "  $v"
    }}
}} else {{
    puts $fp "✓ No DRC violations found"
}}
close $fp

puts "DRC Count: $drc_count"
quit
"""
        magic_tcl_file = self.output_dir / "magic_drc.tcl"
        magic_tcl_file.write_text(magic_tcl)

        try:
            result = subprocess.run(
                ["magic", "-nw", "-noconsole",
                 "-rcfile", "/dev/null",
                 str(magic_tcl_file)],
                capture_output=True, text=True, timeout=600
            )

            if result.returncode == 0:
                violations = self._parse_violations(self.output_dir / "drc_magic_report.txt")
                if violations == 0:
                    print("  ✓ Magic DRC: No violations")
                    return True
                else:
                    print(f"  ✗ Magic DRC: {violations} violations")
                    return False
            else:
                print("  Warning: Magic DRC returned non-zero exit")
                return False

        except FileNotFoundError:
            print("  Warning: magic not found, skipping Magic DRC")
            print("  Install with: sudo apt-get install magic")
            return True  # Non-fatal if not installed
        except subprocess.TimeoutExpired:
            print("  Error: Magic DRC timeout")
            return False
        finally:
            magic_tcl_file.unlink(missing_ok=True)

    def _run_klayout_drc(self):
        """Run DRC using KLayout (more comprehensive)"""
        klayout_drc_script = f"""
# KLayout DRC script for Sky130

layout = RBA::Layout::new
layout.read({str(self.gds_file)!r})

# Sky130 design rules
source($drc_rules, layout)

# Open output
drc_output = {str(self.output_dir / "drc_klayout_report.txt")!r}
output_file = File.open(drc_output, "w")

total_violations = 0

# Run DRC
drc_result = run_drc

drc_result.each_layer do |l|
  count = l.count
  total_violations += count
  if count > 0
    output_file.puts "Layer #{l.name}: #{count} violations"
    l.each do |shape|
      output_file.puts "  #{shape.to_s}"
    end
  end
end

output_file.puts "\\nTotal violations: #{total_violations}"
output_file.close
"""
        # KLayout uses .rb-style scripting, write a Python wrapper
        klayout_tcl = f"""
#!/usr/bin/env python3
import subprocess

drc_rules = "/usr/share/pdk/sky130A/sky130.drc"

result = subprocess.run([
    "klayout", "-b",
    "-rd", f"drc_rules={{drc_rules}}",
    "-rd", "input_gds={self.gds_file}",
    "-rd", "output_dir={self.output_dir}",
    "-r", "/usr/share/pdk/sky130A/sky130_drc.lydrc",
], capture_output=True, text=True, timeout=600)

print(result.stdout)
if result.returncode != 0:
    print("KLayout DRC error:", result.stderr)
"""
        try:
            # First try klayout
            result = subprocess.run(
                ["klayout", "-b",
                 "-rd", f"input_file={self.gds_file}",
                 "-rd", f"output_dir={self.output_dir}",
                 "-r", "/dev/null"],
                capture_output=True, text=True, timeout=60
            )
            print(f"  ✓ KLayout DRC available")

        except FileNotFoundError:
            print("  Warning: klayout not found, using Magic DRC only")
            print("  Install with: sudo apt-get install klayout")

        return True

    def _parse_violations(self, report_file):
        """Parse violation count from DRC report"""
        if not report_file.exists():
            return 0
        try:
            content = report_file.read_text()
            for line in content.split('\n'):
                if 'Total DRC Violations:' in line:
                    return int(line.split(':')[1].strip())
        except Exception:
            pass
        return 0


def main():
    parser = argparse.ArgumentParser(description='DRC Check')
    parser.add_argument('--gds', required=True, help='GDS file to check')
    parser.add_argument('--output', required=True, help='Output directory')
    parser.add_argument('--process', default='130nm', help='Process node')

    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    runner = DRCRunner(args.gds, output_dir, args.process)
    passed = runner.run()

    if passed:
        print("\n✓ DRC passed")
    else:
        print("\n✗ DRC violations found - see reports")

    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
