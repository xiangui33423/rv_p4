/* link.ld — RV-P4 固件链接脚本
 * 香山核片上 SRAM 布局（示例）
 * 实际地址由 SoC 集成决定
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
    SRAM  (rwx) : ORIGIN = 0x80000000, LENGTH = 2M
    MMIO  (rw)  : ORIGIN = 0xA0000000, LENGTH = 64K
}

SECTIONS {
    .text 0x80000000 : {
        *(.text.start)
        *(.text*)
    } > SRAM

    .rodata : {
        *(.rodata*)
    } > SRAM

    .data : {
        *(.data*)
    } > SRAM

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > SRAM

    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
}
