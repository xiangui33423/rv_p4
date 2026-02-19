// qos.c
// QoS 调度配置实现

#include "qos.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 软件状态
// ─────────────────────────────────────────────
static port_qos_cfg_t port_qos[32];
static dscp_map_t     dscp_map;

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

/* 安装 DSCP→队列 TCAM 规则
 *   Stage 5，key=ipv4_dscp(1B)，精确匹配
 *   action=ACTION_SET_PRIO，param[0]=queue
 */
static void install_dscp_rule(uint8_t dscp, uint8_t queue) {
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    e.key.key_len   = 1;
    e.key.bytes[0]  = (uint8_t)(dscp << 2);   // DSCP 占 bits[7:2]，ECN 不关心
    e.mask.key_len  = 1;
    e.mask.bytes[0] = 0xFC;                    // 只匹配高 6 位

    e.stage             = TABLE_DSCP_MAP_STAGE;
    e.table_id          = (uint16_t)(TABLE_DSCP_MAP_BASE + dscp);
    e.action_id         = ACTION_SET_PRIO;
    e.action_params[0]  = queue;               // meta.qos_prio = queue

    hal_tcam_insert(&e);
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void qos_init(void) {
    for (int p = 0; p < 32; p++) {
        port_qos_cfg_t *cfg = &port_qos[p];
        for (int q = 0; q < QOS_QUEUES_PER_PORT; q++)
            cfg->dwrr_weight[q] = QOS_DEFAULT_WEIGHT;
        cfg->pir_bps   = QOS_DEFAULT_PIR;
        cfg->sched_mode = QOS_SCHED_DWRR;
        cfg->sp_queues  = 0;
    }

    qos_dscp_map_default();
    qos_apply_dscp_rules();
    qos_apply_all();
}

int qos_port_set_weights(port_id_t port, const uint32_t *weights) {
    if (port >= 32 || !weights) return HAL_ERR_INVAL;
    for (int q = 0; q < QOS_QUEUES_PER_PORT; q++) {
        port_qos[port].dwrr_weight[q] = weights[q];
        hal_qos_dwrr_set(port, (uint8_t)q, weights[q]);
    }
    return HAL_OK;
}

int qos_port_set_pir(port_id_t port, uint64_t bps) {
    if (port >= 32) return HAL_ERR_INVAL;
    port_qos[port].pir_bps = bps;
    return hal_qos_pir_set(port, bps);
}

int qos_port_set_mode(port_id_t port, uint8_t mode, uint8_t sp_queues) {
    if (port >= 32 || mode > QOS_SCHED_SP_DWRR) return HAL_ERR_INVAL;
    if (sp_queues > QOS_QUEUES_PER_PORT)         return HAL_ERR_INVAL;
    port_qos[port].sched_mode = mode;
    port_qos[port].sp_queues  = sp_queues;
    return hal_qos_sched_mode_set(port, mode);
}

int qos_dscp_set(uint8_t dscp, uint8_t queue) {
    if (dscp >= QOS_DSCP_COUNT || queue >= QOS_QUEUES_PER_PORT)
        return HAL_ERR_INVAL;
    dscp_map.queue[dscp] = queue;
    /* 立即写硬件寄存器和 TCAM 规则 */
    hal_qos_dscp_map_set(dscp, queue);
    install_dscp_rule(dscp, queue);
    return HAL_OK;
}

void qos_dscp_map_default(void) {
    /*
     * RFC 4594 推荐 DSCP → PHB → 8 个队列映射：
     *   Queue 0 (BE)  : CS0(0)、默认
     *   Queue 1 (AF1) : AF11(10)、AF12(12)、AF13(14)
     *   Queue 2 (AF2) : AF21(18)、AF22(20)、AF23(22)
     *   Queue 3 (AF3) : AF31(26)、AF32(28)、AF33(30)
     *   Queue 4 (AF4) : AF41(34)、AF42(36)、AF43(38)
     *   Queue 5 (EF)  : EF(46)、CS5(40)
     *   Queue 6 (NC1) : CS6(48)
     *   Queue 7 (NC2) : CS7(56)
     */
    for (int d = 0; d < QOS_DSCP_COUNT; d++)
        dscp_map.queue[d] = 0;   // 默认 BE

    /* AF1x → Q1 */
    dscp_map.queue[10] = 1; dscp_map.queue[12] = 1; dscp_map.queue[14] = 1;
    /* AF2x → Q2 */
    dscp_map.queue[18] = 2; dscp_map.queue[20] = 2; dscp_map.queue[22] = 2;
    /* AF3x → Q3 */
    dscp_map.queue[26] = 3; dscp_map.queue[28] = 3; dscp_map.queue[30] = 3;
    /* AF4x → Q4 */
    dscp_map.queue[34] = 4; dscp_map.queue[36] = 4; dscp_map.queue[38] = 4;
    /* CS5 + EF → Q5 */
    dscp_map.queue[40] = 5; dscp_map.queue[46] = 5;
    /* CS6 → Q6 */
    dscp_map.queue[48] = 6;
    /* CS7 → Q7 */
    dscp_map.queue[56] = 7;
}

void qos_apply_dscp_rules(void) {
    for (int d = 0; d < QOS_DSCP_COUNT; d++) {
        hal_qos_dscp_map_set((uint8_t)d, dscp_map.queue[d]);
        install_dscp_rule((uint8_t)d, dscp_map.queue[d]);
    }
}

void qos_apply_port(port_id_t port) {
    if (port >= 32) return;
    port_qos_cfg_t *cfg = &port_qos[port];

    for (int q = 0; q < QOS_QUEUES_PER_PORT; q++)
        hal_qos_dwrr_set(port, (uint8_t)q, cfg->dwrr_weight[q]);

    hal_qos_pir_set(port, cfg->pir_bps);
    hal_qos_sched_mode_set(port, cfg->sched_mode);
}

void qos_apply_all(void) {
    for (int p = 0; p < 32; p++)
        qos_apply_port((port_id_t)p);
}

void qos_show_port(port_id_t port) {
    if (port >= 32) return;
    port_qos_cfg_t *cfg = &port_qos[port];
    static const char *modes[] = {"DWRR", "SP", "SP+DWRR"};
    printf("Port%2d  mode=%-8s  PIR=%llu bps\n",
           port, modes[cfg->sched_mode < 3 ? cfg->sched_mode : 0],
           (unsigned long long)cfg->pir_bps);
    printf("  Queue weights: ");
    for (int q = 0; q < QOS_QUEUES_PER_PORT; q++)
        printf("Q%d=%u  ", q, cfg->dwrr_weight[q]);
    printf("\n");
}
