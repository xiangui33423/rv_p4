# Makefile — RV-P4 控制面固件
# 目标：RISC-V 64-bit bare-metal ELF

CC      = riscv64-unknown-elf-gcc
CFLAGS  = -O2 -march=rv64gc -mabi=lp64d -Wall -Wextra \
          -I../hal -I. -ffreestanding -nostdlib
LDFLAGS = -T link.ld -nostdlib

SRCS    = cp_main.c       \
          ../hal/rv_p4_hal.c \
          vlan.c          \
          arp.c           \
          qos.c           \
          fdb.c           \
          route.c         \
          acl.c           \
          cli.c           \
          cli_cmds.c
OBJS    = $(SRCS:.c=.o)
TARGET  = cp_firmware.elf

.PHONY: all clean sim test

all: $(TARGET)

# 运行 host 端单元测试（无需 RISC-V 工具链）
test:
	$(MAKE) -C test

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 本机 x86 仿真构建（用于手工调试，不依赖 RISC-V 工具链）
sim:
	gcc -O2 -Wall -I../hal -I. -DSIM_MODE \
	    -o cp_firmware_sim \
	    cp_main.c ../hal/rv_p4_hal.c \
	    vlan.c arp.c qos.c fdb.c route.c acl.c cli.c cli_cmds.c

clean:
	rm -f $(OBJS) $(TARGET) cp_firmware_sim
	$(MAKE) -C test clean
