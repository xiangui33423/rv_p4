// route.h
// IPv4 路由管理模块
// 维护 LPM 路由软件表，并向 Stage 0 TCAM 安装转发规则

#ifndef ROUTE_H
#define ROUTE_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define ROUTE_TABLE_SIZE  256     // 软件路由表容量

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * route_init - 清空路由软件状态
 */
void route_init(void);

/**
 * route_add - 添加/更新一条 IPv4 LPM 路由
 * @prefix: 网络地址（主机字节序大端 uint32，如 10.0.0.0 = 0x0A000000）
 * @len:    前缀长度（0-32）
 * @port:   出端口
 * @dmac:   下一跳 MAC（48-bit，高 16 位为 0）
 * 返回 HAL_OK 或错误码
 */
int route_add(uint32_t prefix, uint8_t len, uint8_t port, uint64_t dmac);

/**
 * route_del - 删除路由，并从 TCAM 撤销规则
 * @prefix/@len: 与添加时一致
 */
int route_del(uint32_t prefix, uint8_t len);

/**
 * route_show - 打印路由表（调试 / CLI show route）
 */
void route_show(void);

#endif /* ROUTE_H */
