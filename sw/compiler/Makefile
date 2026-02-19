# Makefile — RV-P4 C-to-HW 编译器

COMPILER = python3 rvp4cc.py
EXAMPLE  = example_dataplane.c
HWCFG    = dataplane.hwcfg

.PHONY: all test clean

all: $(HWCFG)

$(HWCFG): $(EXAMPLE) rvp4cc.py
	$(COMPILER) $(EXAMPLE) -o $(HWCFG) --report --dump-json

test: $(HWCFG)
	@echo "=== Verifying output files ==="
	@python3 -c "\
import tarfile, sys; \
tf = tarfile.open('$(HWCFG)'); \
names = tf.getnames(); \
required = ['parser_tcam.bin','phv_map.json','table_info.json','action_info.json']; \
missing = [r for r in required if r not in names]; \
print('Files in hwcfg:', len(names)); \
sys.exit(1) if missing else print('OK: all required files present')"

clean:
	rm -f $(HWCFG) phv_map.json table_info.json action_info.json dataplane.report
