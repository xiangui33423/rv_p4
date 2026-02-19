// pkt_model.h
// 数据面功能模型 — 用于控制面 + 数据面联合测试
//
// 实现一个纯软件的 PISA 流水线仿真器，与 sim_hal.c 的 TCAM 数据库对接：
//   1. 将原始以太帧解析为 PHV（Packet Header Vector）
//   2. 对每个 MAU Stage 执行三值 TCAM 查找（key & mask 匹配）
//   3. 执行命中的 Action，更新 PHV 元数据（egress port、drop、vlan_id 等）
//   4. 返回最终转发决策
//
// 注意：仅覆盖当前固件使用的 7 个 Stage（Stage 0-6），不模拟 Stage 7-23。

#ifndef PKT_MODEL_H
#define PKT_MODEL_H

#include <stdint.h>
#include "table_map.h"
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────

#define PKT_PHV_HDR_SIZE  512   // PHV 报头区（与 rv_p4_pkg.sv PHV_BYTES 一致）
#define PKT_NUM_STAGES    7     // 固件使用的 MAU 级数（0-6）

// VLAN 出口动作（存入 phv.vlan_action）
#define VLAN_ACT_NONE   0
#define VLAN_ACT_STRIP  1   // Access 出口：剥离标签
#define VLAN_ACT_KEEP   2   // Trunk 出口：保留标签

// ─────────────────────────────────────────────
// PHV 内部表示
// ─────────────────────────────────────────────
typedef struct {
    uint8_t  hdr[PKT_PHV_HDR_SIZE]; // 报头字段，偏移与 table_map.h PHV_OFF_* 对齐
    // 元数据（镜像 PHV_OFF_* 定义的元数据区）
    uint8_t  ig_port;       // 入端口
    uint8_t  eg_port;       // 出端口（默认 0，由路由或 FDB Action 写入）
    uint8_t  drop;          // 1 = 丢弃
    uint8_t  punt;          // 1 = 上送 CPU
    uint16_t vlan_id;       // 当前报文的 VLAN ID（由 Stage 4 写入）
    uint8_t  qos_prio;      // QoS 优先级队列（由 Stage 5 写入）
    uint8_t  vlan_action;   // VLAN 出口动作（由 Stage 6 写入，VLAN_ACT_*）
} phv_t;

// ─────────────────────────────────────────────
// 转发决策（pkt_forward 的输出）
// ─────────────────────────────────────────────
typedef struct {
    uint8_t  eg_port;       // 出端口
    uint8_t  drop;          // 1 = 丢弃
    uint8_t  punt;          // 1 = 上送 CPU
    uint16_t vlan_id;       // 处理后 VLAN ID
    uint8_t  qos_prio;      // QoS 队列优先级
    uint8_t  vlan_action;   // 出口 VLAN 动作（VLAN_ACT_*）
} fwd_result_t;

// ─────────────────────────────────────────────
// 公共 API
// ─────────────────────────────────────────────

/**
 * pkt_parse - 将原始以太帧解析为 PHV
 * @raw:      原始报文字节数组（含以太网头）
 * @raw_len:  报文字节数（至少 14 字节）
 * @ing_port: 入端口号（0-31）
 * @phv:      输出的 PHV（调用者分配）
 * 返回 0 表示成功，-1 表示报文过短
 */
int pkt_parse(const uint8_t *raw, uint16_t raw_len,
              uint8_t ing_port, phv_t *phv);

/**
 * pkt_forward - 对已解析的 PHV 执行 7 级 MAU 流水线仿真
 * @phv:    入/出参数：输入已解析的 PHV，输出经 Action 修改后的 PHV
 * @result: 最终转发决策
 * 返回 0（始终成功；drop/punt 通过 result 字段表示）
 */
int pkt_forward(phv_t *phv, fwd_result_t *result);

/**
 * pkt_process - pkt_parse + pkt_forward 的一步封装
 */
int pkt_process(const uint8_t *raw, uint16_t raw_len,
                uint8_t ing_port, fwd_result_t *result);

#endif /* PKT_MODEL_H */
