# Makefile — RV-P4 控制面固件单元测试
# 编译目标：x86 host（无需 RISC-V 工具链）
# 运行：make  或  make test

CC      = gcc
CFLAGS  = -O0 -g -Wall -Wextra -Wno-unused-parameter \
          -I../../hal -I.. -DSIM_MODE

# 被测模块（从 firmware 目录引入）
MODULE_SRCS = ../vlan.c   \
              ../arp.c    \
              ../qos.c    \
              ../fdb.c    \
              ../route.c  \
              ../acl.c    \
              ../cli_cmds.c

# 模拟 HAL + 测试用例
TEST_SRCS = sim_hal.c           \
            test_main.c         \
            test_vlan.c         \
            test_arp.c          \
            test_qos.c          \
            test_route.c        \
            test_acl.c          \
            test_cli.c          \
            test_integration.c

TARGET = run_tests

.PHONY: all test clean

all: test

test: $(TARGET)
	@echo ""
	@./$(TARGET)

$(TARGET): $(TEST_SRCS) $(MODULE_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o
