// cli_cmds.h
// CLI 命令分发接口

#ifndef CLI_CMDS_H
#define CLI_CMDS_H

/**
 * cli_exec_cmd - 执行一条已分词的命令
 * @argc: 参数数量（argv[0] 为命令名）
 * @argv: 参数数组（均为 NUL 结尾字符串）
 * 返回 1：命令已识别（不论是否成功）
 *      0：未知命令
 */
int cli_exec_cmd(int argc, char **argv);

#endif /* CLI_CMDS_H */
