# Makefile — RV-P4 HAL 库

CC      = riscv64-unknown-elf-gcc
CFLAGS  = -O2 -march=rv64gc -mabi=lp64d -Wall -I.

LIB     = librv_p4_hal.a
SRCS    = rv_p4_hal.c
OBJS    = rv_p4_hal.o

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	riscv64-unknown-elf-ar rcs $@ $^

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
