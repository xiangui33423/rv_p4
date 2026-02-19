// vlan.h
// VLAN 管理模块 — 控制面 VLAN 数据库 + 数据面规则安装
// 裸机固件，运行在香山 RISC-V 核上

#ifndef VLAN_H
#define VLAN_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define VLAN_MAX_ID         255     // 支持 VLAN 1-255（0 保留）
#define VLAN_DEFAULT_ID     1       // 默认 VLAN
#define VLAN_INVALID        0xFFFF

// VLAN 入口规则：每端口 2 条（带标签帧 + 无标签帧）
// TCAM 索引 = VLAN_INGRESS_ENTRY(port, is_tagged)
#define VLAN_INGRESS_ENTRY(port, tagged)  ((port) * 2 + (tagged))

// VLAN 出口规则：每 (port, vlan) 一条
#define VLAN_EGRESS_ENTRY(port, vlan)     ((port) * 256 + (vlan))

// ─────────────────────────────────────────────
// 数据结构
// ─────────────────────────────────────────────

// VLAN 数据库条目
typedef struct {
    uint32_t member_bitmap;     // bit[p]=1：端口 p 是该 VLAN 成员
    uint32_t untagged_bitmap;   // bit[p]=1：端口 p 出口不打标签（access 模式）
    uint8_t  valid;
} vlan_entry_t;

// 端口 VLAN 配置
typedef struct {
    uint16_t pvid;      // Native VLAN（无标签帧分类到此 VLAN）
    uint8_t  mode;      // VLAN_MODE_ACCESS 或 VLAN_MODE_TRUNK
} port_vlan_cfg_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * vlan_init - 初始化 VLAN 数据库
 *   创建默认 VLAN 1，所有端口以 access 模式加入
 */
void vlan_init(void);

/**
 * vlan_create - 创建 VLAN
 * @vlan_id: 1-255
 * 返回 HAL_OK 或错误码
 */
int vlan_create(uint16_t vlan_id);

/**
 * vlan_delete - 删除 VLAN
 *   同时将所有成员端口从该 VLAN 移除，删除 MAU 规则
 */
int vlan_delete(uint16_t vlan_id);

/**
 * vlan_port_add - 将端口加入 VLAN
 * @vlan_id: 目标 VLAN
 * @port:    端口号（0-31）
 * @tagged:  0=无标签出（access），1=带标签出（trunk）
 */
int vlan_port_add(uint16_t vlan_id, port_id_t port, uint8_t tagged);

/**
 * vlan_port_remove - 将端口从 VLAN 移除
 */
int vlan_port_remove(uint16_t vlan_id, port_id_t port);

/**
 * vlan_port_set_pvid - 配置端口 Native VLAN
 * @port:    端口号
 * @vlan_id: PVID（1-255）
 */
int vlan_port_set_pvid(port_id_t port, uint16_t vlan_id);

/**
 * vlan_port_set_mode - 配置端口工作模式
 * @mode: VLAN_MODE_ACCESS 或 VLAN_MODE_TRUNK
 *   access：入口仅接受无标签帧（打上 PVID），出口剥离标签
 *   trunk ：入口接受所有 VLAN 帧，出口保留标签
 */
int vlan_port_set_mode(port_id_t port, uint8_t mode);

/**
 * vlan_install_port_rules - 向数据面安装指定端口的 VLAN 入口+出口规则
 *   内部调用 hal_tcam_insert()，在 stage 4（入口）和 stage 6（出口）写规则
 */
void vlan_install_port_rules(port_id_t port);

/**
 * vlan_show - 打印 VLAN 数据库（调试用）
 */
void vlan_show(void);

#endif /* VLAN_H */
