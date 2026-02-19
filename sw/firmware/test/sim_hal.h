// sim_hal.h
// 模拟 HAL — 用于 host 端单元测试
// 替换真实 MMIO 驱动，记录所有操作供测试验证

#ifndef SIM_HAL_H
#define SIM_HAL_H

#include "rv_p4_hal.h"
#include <stdint.h>

// ─────────────────────────────────────────────
// 容量
// ─────────────────────────────────────────────
#define SIM_TCAM_MAX    512     // TCAM 记录总槽数
#define SIM_PUNT_MAX    32      // Punt 环槽数

// ─────────────────────────────────────────────
// TCAM 记录
// ─────────────────────────────────────────────
typedef struct {
    tcam_entry_t entry;
    uint8_t      valid;     // 1 = 槽已占用
    uint8_t      deleted;   // 1 = 已通过 delete 标记删除
} sim_tcam_rec_t;

// ─────────────────────────────────────────────
// Punt 包记录
// ─────────────────────────────────────────────
typedef struct {
    punt_pkt_t pkt;
    uint8_t    valid;
} sim_punt_rec_t;

// ─────────────────────────────────────────────
// 模拟状态（extern 声明，在 sim_hal.c 中定义）
// ─────────────────────────────────────────────

/* TCAM 数据库 */
extern sim_tcam_rec_t sim_tcam_db[SIM_TCAM_MAX];
extern int            sim_tcam_n;   // 已分配槽数（含已删除）

/* VLAN CSR */
extern uint16_t  sim_vlan_pvid[32];
extern uint8_t   sim_vlan_mode[32];
extern uint32_t  sim_vlan_member[256];
extern uint32_t  sim_vlan_untagged[256];

/* QoS CSR */
extern uint32_t  sim_qos_dwrr[32][8];
extern uint64_t  sim_qos_pir[32];   /* uint64 支持 10Gbps+ */
extern uint8_t   sim_qos_mode[32];
extern uint8_t   sim_qos_dscp_map[64];

/* 端口使能寄存器 */
extern uint32_t  sim_port_enable;

/* Punt RX 环（test→firmware）*/
extern sim_punt_rec_t sim_punt_rx_ring[SIM_PUNT_MAX];
extern int            sim_punt_rx_head;   /* test 注入指针 */
extern int            sim_punt_rx_tail;   /* firmware 消费指针 */

/* Punt TX 环（firmware→test）*/
extern sim_punt_rec_t sim_punt_tx_ring[SIM_PUNT_MAX];
extern int            sim_punt_tx_head;   /* firmware 写入指针 */
extern int            sim_punt_tx_tail;   /* test 读取指针 */

// ─────────────────────────────────────────────
// 控制函数
// ─────────────────────────────────────────────

/** 重置所有模拟状态（每个测试用例前调用） */
void sim_hal_reset(void);

/** 注入一个包到 Punt RX 环（模拟数据面 punt 行为） */
void sim_punt_rx_inject(const punt_pkt_t *pkt);

/** 查找 TCAM 条目（跳过已删除项），找不到返回 NULL */
sim_tcam_rec_t *sim_tcam_find(uint8_t stage, uint16_t table_id);

/** 统计某 stage 的有效（未删除）TCAM 条目数 */
int sim_tcam_count_stage(uint8_t stage);

// ─────────────────────────────────────────────
// 内联辅助（供 test_*.c 使用）
// ─────────────────────────────────────────────

static inline int sim_punt_tx_pending(void) {
    return sim_punt_tx_head - sim_punt_tx_tail;
}

static inline sim_punt_rec_t *sim_punt_tx_pop(void) {
    if (sim_punt_tx_tail >= sim_punt_tx_head) return NULL;
    int slot = sim_punt_tx_tail % SIM_PUNT_MAX;
    sim_punt_tx_tail++;
    return &sim_punt_tx_ring[slot];
}

#endif /* SIM_HAL_H */
