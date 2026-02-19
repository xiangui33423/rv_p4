// qos.h
// QoS 调度配置模块
// 管理：DWRR 队列权重、端口峰值速率（PIR）、DSCP→队列优先级映射
//
// 数据面联动：
//   DSCP 映射以 TCAM 规则安装到 Stage 5（TABLE_DSCP_MAP_STAGE）
//   DWRR 权重和 PIR 写入 TM CSR（通过 HAL_BASE_QOS）

#ifndef QOS_H
#define QOS_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define QOS_QUEUES_PER_PORT  8      // 每端口 8 个队列（优先级 0=最低）
#define QOS_DSCP_COUNT       64     // DSCP 值范围 0-63
#define QOS_DEFAULT_WEIGHT   1500   // 默认 DWRR 权重（bytes，约 1 个 MTU）
#define QOS_DEFAULT_PIR      0      // 0 = 不限速

// ─────────────────────────────────────────────
// 数据结构
// ─────────────────────────────────────────────

// 端口 QoS 配置
typedef struct {
    uint32_t dwrr_weight[QOS_QUEUES_PER_PORT]; // 各队列 DWRR 权重（bytes）
    uint64_t pir_bps;                           // 端口峰值速率（0=不限）
    uint8_t  sched_mode;                        // QOS_SCHED_*
    uint8_t  sp_queues;                         // SP 队列数（高优先级队列数）
} port_qos_cfg_t;

// DSCP → 队列映射表（64 项）
typedef struct {
    uint8_t queue[QOS_DSCP_COUNT]; // queue[dscp] = 目标队列（0-7）
} dscp_map_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * qos_init - 初始化 QoS 模块（默认配置 + 写硬件）
 *   默认：所有端口 DWRR，权重均等，不限速，DSCP 按 RFC 4594 建议映射
 */
void qos_init(void);

/**
 * qos_port_set_weights - 配置端口 DWRR 权重
 * @weights: 长度为 QOS_QUEUES_PER_PORT 的数组，单位 bytes
 *   权重越大，该队列获得的带宽份额越多
 */
int qos_port_set_weights(port_id_t port, const uint32_t *weights);

/**
 * qos_port_set_pir - 配置端口峰值速率
 * @bps: 比特/秒；0 = 不限速
 */
int qos_port_set_pir(port_id_t port, uint64_t bps);

/**
 * qos_port_set_mode - 配置端口调度模式
 * @mode:      QOS_SCHED_DWRR / QOS_SCHED_SP / QOS_SCHED_SP_DWRR
 * @sp_queues: 当 mode=SP_DWRR 时，高 sp_queues 个队列走 SP，其余 DWRR
 */
int qos_port_set_mode(port_id_t port, uint8_t mode, uint8_t sp_queues);

/**
 * qos_dscp_set - 配置单个 DSCP 值的队列映射
 * @dscp:  0-63
 * @queue: 0-7（0 最低优先级）
 */
int qos_dscp_set(uint8_t dscp, uint8_t queue);

/**
 * qos_dscp_map_default - 加载 RFC 4594 推荐的 DSCP→队列映射
 *   CS0/默认 → Q0, AF11-13 → Q1, AF21-23 → Q2,
 *   AF31-33 → Q3, AF41-43 → Q4, CS5/EF → Q5, CS6 → Q6, CS7/NC → Q7
 */
void qos_dscp_map_default(void);

/**
 * qos_apply_dscp_rules - 将 DSCP 映射表写入 MAU Stage 5 TCAM 规则
 *   每个 DSCP 值对应一条规则：match(ipv4_dscp) → set(meta.qos_prio)
 */
void qos_apply_dscp_rules(void);

/**
 * qos_apply_port - 将指定端口的 QoS 配置写入 TM CSR
 */
void qos_apply_port(port_id_t port);

/**
 * qos_apply_all - 对所有端口调用 qos_apply_port()
 */
void qos_apply_all(void);

/**
 * qos_show_port - 打印端口 QoS 配置（调试）
 */
void qos_show_port(port_id_t port);

#endif /* QOS_H */
