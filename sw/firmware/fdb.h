// fdb.h
// L2 转发数据库（FDB）管理模块
// 跟踪 MAC→端口映射，并向 Stage 2 TCAM 安装转发规则

#ifndef FDB_H
#define FDB_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define FDB_TABLE_SIZE    256     // 软件 FDB 表容量
#define FDB_AGE_DYNAMIC   300     // 动态条目老化时间（秒）

// ─────────────────────────────────────────────
// 数据结构
// ─────────────────────────────────────────────
typedef struct {
    uint64_t  dmac;         // 48-bit MAC（高 16 位为 0）
    port_id_t port;
    uint16_t  vlan;
    uint32_t  age_ticks;    // 最后活跃时间（秒，来自主循环计数器）
    uint8_t   is_static;    // 1=静态（不老化），0=动态
    uint8_t   valid;
} fdb_entry_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * fdb_init - 清空 FDB 软件状态
 *   每次系统初始化时调用一次
 */
void fdb_init(void);

/**
 * fdb_learn - 动态学习 MAC 条目（被 arp.c 调用）
 *   同时安装 TCAM 规则到 Stage 2（L2 FDB）
 * 返回 HAL_OK 或 HAL_ERR_FULL
 */
int fdb_learn(uint64_t dmac, uint8_t port);

/**
 * fdb_add_static - 添加静态 MAC 条目（不老化）
 * @vlan: 所属 VLAN（仅用于显示，不影响 TCAM 规则）
 */
int fdb_add_static(uint64_t dmac, uint8_t port, uint16_t vlan);

/**
 * fdb_delete - 删除条目，并从 TCAM 撤销规则
 */
int fdb_delete(uint64_t dmac);

/**
 * fdb_age - 周期性老化（每秒调用）
 *   删除超过 FDB_AGE_DYNAMIC 秒未活跃的动态条目
 */
void fdb_age(uint32_t now_sec);

/**
 * fdb_show - 打印 FDB 表（调试 / CLI show fdb）
 */
void fdb_show(void);

#endif /* FDB_H */
