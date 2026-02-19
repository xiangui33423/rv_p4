// cli.h
// 控制台 CLI — 行编辑 + 命令分发框架
// 运行在香山 RISC-V 裸机固件上，通过 UART 与用户交互

#ifndef CLI_H
#define CLI_H

#define CLI_LINE_MAX    128     // 最大输入行长度（含 NUL）
#define CLI_PROMPT      "rv-p4> "

/**
 * cli_init - 初始化 CLI 模块，打印欢迎信息和首次提示符
 *   在 main() 初始化所有模块后调用一次
 */
void cli_init(void);

/**
 * cli_poll - 在主循环中轮询 UART RX，处理输入字符
 *   当收到回车时解析并分发命令，打印结果后再次显示提示符
 *   每次主循环迭代调用一次（非阻塞）
 */
void cli_poll(void);

#endif /* CLI_H */
