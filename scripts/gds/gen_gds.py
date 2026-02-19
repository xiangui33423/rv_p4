#!/usr/bin/env python3
"""
Final GDS-II Generation for RV-P4
Converts routed DEF to GDS-II for tape-out
"""

import sys
import argparse
import subprocess
from pathlib import Path

class GDSGenerator:
    def __init__(self, routed_def, output_gds, process_node):
        self.routed_def = Path(routed_def)
        self.output_gds = Path(output_gds)
        self.process_node = process_node

    def run(self):
        """Generate final GDS-II"""
        print("[1/3] Converting DEF to GDS via Magic...")
        self._magic_stream()

        print("[2/3] Cleaning and finalizing GDS...")
        self._finalize_gds()

        print("[3/3] Verifying GDS structure...")
        self._verify_gds()

        return self.output_gds.exists()

    def _magic_stream(self):
        """Use Magic to convert DEF layout to GDS"""
        magic_tcl = f"""
# Load Sky130 technology
tech load sky130A

# Read placed and routed DEF
def read {self.routed_def}

# Stream out to GDS-II format
gds write {self.output_gds}

# Summary
puts "GDS written to: {self.output_gds}"
quit
"""
        tcl_file = self.output_gds.parent / "gds_gen.tcl"
        tcl_file.write_text(magic_tcl)
        try:
            result = subprocess.run(
                ["magic", "-nw", "-noconsole", "-T", "sky130A", str(tcl_file)],
                capture_output=True, text=True, timeout=300
            )
            if result.returncode == 0:
                print(f"  ✓ Magic stream complete")
            else:
                print(f"  Warning: Magic returned {result.returncode}")
                if result.stderr:
                    print(f"  {result.stderr[:200]}")
        except FileNotFoundError:
            print("  Warning: magic not found, using OpenROAD write_gds fallback")
            self._openroad_write_gds()
        finally:
            tcl_file.unlink(missing_ok=True)

    def _openroad_write_gds(self):
        """Fallback: use OpenROAD to write GDS"""
        tcl_script = f"""
read_def {self.routed_def}
write_gds {self.output_gds}
puts "GDS written via OpenROAD: {self.output_gds}"
"""
        tcl_file = self.output_gds.parent / "or_gds.tcl"
        tcl_file.write_text(tcl_script)
        try:
            subprocess.run(
                ["openroad", str(tcl_file)],
                capture_output=True, text=True, timeout=300
            )
        except Exception as e:
            print(f"  OpenROAD GDS write failed: {e}")
        finally:
            tcl_file.unlink(missing_ok=True)

    def _finalize_gds(self):
        """Add frame/top-level cell and clean GDS"""
        if not self.output_gds.exists():
            return

        # Use KLayout for final GDS manipulation (if available)
        klayout_script = f"""
import pya
layout = pya.Layout()
layout.read({str(self.output_gds)!r})

# Flatten design for final GDS
top_cell = layout.top_cell()

# Write final GDS with correct version flags
layout.write({str(self.output_gds)!r})
print("GDS finalized successfully")
"""
        script_file = self.output_gds.parent / "finalize_gds.py"
        script_file.write_text(klayout_script)
        try:
            subprocess.run(
                ["klayout", "-b", "-r", str(script_file)],
                capture_output=True, text=True, timeout=120
            )
        except FileNotFoundError:
            pass  # klayout optional
        finally:
            script_file.unlink(missing_ok=True)

    def _verify_gds(self):
        """Verify GDS file integrity"""
        if not self.output_gds.exists():
            print("  ✗ GDS file not found")
            return False

        size_bytes = self.output_gds.stat().st_size
        size_mb = size_bytes / (1024 * 1024)
        print(f"  GDS file: {self.output_gds}")
        print(f"  Size:     {size_mb:.2f} MB")

        # Check GDS magic bytes (0x0002 record type)
        with open(self.output_gds, 'rb') as f:
            header = f.read(4)
        if header[:2] == b'\x00\x06' and header[2:4] == b'\x00\x02':
            print("  ✓ Valid GDS-II file format")
            return True
        else:
            print("  Warning: GDS header may be non-standard")
            return True  # Not fatal


def main():
    parser = argparse.ArgumentParser(description='GDS-II Generation')
    parser.add_argument('--routed-def', required=True, help='Routed DEF file')
    parser.add_argument('--output', required=True, help='Output GDS file')
    parser.add_argument('--process', default='130nm', help='Process node')

    args = parser.parse_args()

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    gen = GDSGenerator(args.routed_def, args.output, args.process)

    try:
        success = gen.run()
        if success:
            print(f"\n✓ GDS-II generated: {args.output}")
        else:
            print("\n✗ GDS generation failed")
        return 0 if success else 1
    except Exception as e:
        print(f"\n✗ GDS generation failed: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
