# Makefile — RV-P4 RTL Co-Simulation
# Links Verilator-compiled data-plane RTL with C control-plane firmware.
#
# Build:  make
# Run:    make test
# Clean:  make clean

VERILATOR  = verilator
REPO_ROOT  = ../..
RTL_DIR    = $(REPO_ROOT)/rtl
FW_DIR     = $(REPO_ROOT)/sw/firmware
HAL_DIR    = $(REPO_ROOT)/sw/hal
INC_DIR    = $(RTL_DIR)/include
OBJ_DIR    = obj_dir
TARGET     = cosim_sim

# Flags forwarded to every C/C++ source compiled inside the Verilated build.
# -DSIM_MODE : firmware compile-time guard (same flag used by sw/firmware/test/).
# Include paths ensure firmware headers find rv_p4_hal.h, table_map.h, etc.
EXTRA_CFLAGS = -DSIM_MODE \
               -I$(abspath $(HAL_DIR)) \
               -I$(abspath $(FW_DIR))

# RTL source list.
# NOTE: mac_rx_arb and rst_sync are taken from rtl/common/ (more complete FSM
# implementation) rather than rtl/top/ to avoid duplicate module definitions.
# ctrl_plane.sv is from rtl/top/ which has the tb_tue_* backdoor additions.
RTL_SRCS = \
  $(RTL_DIR)/top/rv_p4_top.sv        \
  $(RTL_DIR)/top/ctrl_plane.sv       \
  $(RTL_DIR)/common/mac_rx_arb.sv    \
  $(RTL_DIR)/common/rst_sync.sv      \
  $(RTL_DIR)/parser/p4_parser.sv     \
  $(RTL_DIR)/parser/parser_tcam.sv   \
  $(RTL_DIR)/mau/mau_stage.sv        \
  $(RTL_DIR)/mau/mau_alu.sv          \
  $(RTL_DIR)/mau/mau_tcam.sv         \
  $(RTL_DIR)/mau/mau_hash.sv         \
  $(RTL_DIR)/tue/tue.sv              \
  $(RTL_DIR)/pkt_buffer/pkt_buffer.sv \
  $(RTL_DIR)/tm/traffic_manager.sv   \
  $(RTL_DIR)/deparser/deparser.sv

# Firmware C modules (no rv_p4_hal.c — HAL is implemented in cosim_main.cpp)
FW_SRCS = \
  $(FW_DIR)/route.c \
  $(FW_DIR)/fdb.c   \
  $(FW_DIR)/acl.c   \
  $(FW_DIR)/qos.c   \
  $(FW_DIR)/arp.c   \
  $(FW_DIR)/vlan.c

.PHONY: all test clean

all: $(TARGET)

test: $(TARGET)
	@echo ""
	@./$(TARGET)

# -----------------------------------------------------------------------------
# Build rule: Verilate RTL + compile firmware + link cosim harness.
#
# Step 1 (verilator --cc --exe): parse all SV, generate Vrv_p4_top.mk and
#   supporting C++ in $(OBJ_DIR)/; embed cosim_main.cpp and firmware .c files
#   as user sources; embed EXTRA_CFLAGS into the generated Makefile.
# Step 2 ($(MAKE) -C): compile and link everything using the generated
#   Vrv_p4_top.mk (handles Verilated runtime, user C/C++ files, flags).
# Step 3 (cp): promote the resulting binary to the current directory.
# -----------------------------------------------------------------------------
$(TARGET): cosim_main.cpp $(FW_SRCS) $(RTL_SRCS)
	$(VERILATOR) --cc --exe                                  \
	  +incdir+$(abspath $(INC_DIR))                          \
	  --top-module rv_p4_top                                 \
	  --Mdir $(OBJ_DIR)                                      \
	  --CFLAGS "$(EXTRA_CFLAGS)"                             \
	  -Wno-MULTIDRIVEN                                       \
	  -Wno-UNOPTFLAT                                         \
	  -Wno-WIDTHTRUNC                                        \
	  -Wno-WIDTHEXPAND                                       \
	  -Wno-UNUSED                                            \
	  -Wno-PINMISSING                                        \
	  -Wno-TIMESCALEMOD                                      \
	  -Wno-IMPLICITSTATIC                                    \
	  cosim_main.cpp                                         \
	  $(FW_SRCS)                                             \
	  $(RTL_SRCS)
	$(MAKE) -C $(OBJ_DIR) -f Vrv_p4_top.mk OBJCACHE=
	cp $(OBJ_DIR)/Vrv_p4_top $(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf $(OBJ_DIR)
