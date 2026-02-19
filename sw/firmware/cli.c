// cli.c
// CLI 行编辑器 + 命令分发主体

#include "cli.h"
#include "cli_cmds.h"
#include "rv_p4_hal.h"
#include <string.h>

// ─────────────────────────────────────────────
// 内部状态
// ─────────────────────────────────────────────
static char cli_buf[CLI_LINE_MAX];
static int  cli_pos;

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

/* 分词并分发一行输入 */
static void cli_dispatch(char *line) {
    char *argv[16];
    int   argc = 0;
    char *p    = line;

    while (*p && argc < 16) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }

    if (argc == 0) {
        hal_uart_puts(CLI_PROMPT);
        return;
    }

    if (!cli_exec_cmd(argc, argv))
        hal_uart_puts("Unknown command. Type 'help' for help.\r\n");
    hal_uart_puts(CLI_PROMPT);
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void cli_init(void) {
    cli_pos = 0;
    hal_uart_puts("\r\nRV-P4 Control Plane v1.0\r\n");
    hal_uart_puts(CLI_PROMPT);
}

void cli_poll(void) {
    int c;
    while ((c = hal_uart_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            hal_uart_puts("\r\n");
            cli_buf[cli_pos] = '\0';
            cli_dispatch(cli_buf);
            cli_pos = 0;
        } else if ((c == 0x7F || c == '\b') && cli_pos > 0) {
            /* Backspace：回退并擦除 */
            cli_pos--;
            hal_uart_puts("\b \b");
        } else if (c >= 0x20 && cli_pos < CLI_LINE_MAX - 1) {
            cli_buf[cli_pos++] = (char)c;
            char echo[2] = {(char)c, '\0'};
            hal_uart_puts(echo);
        }
    }
}
