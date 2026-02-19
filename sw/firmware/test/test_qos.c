// test_qos.c
// QoS 调度配置模块测试用例（5 个）
//
// 用例列表：
//   1. test_qos_dscp_default_map   — RFC 4594 默认 DSCP→队列映射正确性
//   2. test_qos_dscp_tcam_rules    — qos_init() 安装 64 条 TCAM 规则
//   3. test_qos_dscp_rule_encoding — TCAM key 编码：高 6 位 + 掩码 0xFC
//   4. test_qos_dwrr_weights       — 默认权重 + 自定义权重写入 CSR
//   5. test_qos_port_pir_mode      — PIR 速率 + 调度模式配置及边界检查

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "qos.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// TC-QOS-1: RFC 4594 默认 DSCP 映射
// ─────────────────────────────────────────────
void test_qos_dscp_default_map(void) {
    TEST_BEGIN("QOS-1 : RFC 4594 default DSCP → queue mapping");

    sim_hal_reset();
    qos_init();

    /*
     * RFC 4594 期望映射：
     *   Q0 (BE)  : DSCP  0  (CS0/Default)
     *   Q1 (AF1) : DSCP 10  (AF11)
     *   Q2 (AF2) : DSCP 18  (AF21)
     *   Q3 (AF3) : DSCP 26  (AF31)
     *   Q4 (AF4) : DSCP 34  (AF41)
     *   Q5 (EF)  : DSCP 46  (EF), DSCP 40 (CS5)
     *   Q6       : DSCP 48  (CS6)
     *   Q7 (NC)  : DSCP 56  (CS7)
     */
    TEST_ASSERT_EQ(sim_qos_dscp_map[0],  0);   /* Default → BE */
    TEST_ASSERT_EQ(sim_qos_dscp_map[10], 1);   /* AF11 → Q1 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[12], 1);   /* AF12 → Q1 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[14], 1);   /* AF13 → Q1 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[18], 2);   /* AF21 → Q2 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[22], 2);   /* AF23 → Q2 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[26], 3);   /* AF31 → Q3 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[34], 4);   /* AF41 → Q4 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[38], 4);   /* AF43 → Q4 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[40], 5);   /* CS5  → Q5 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[46], 5);   /* EF   → Q5 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[48], 6);   /* CS6  → Q6 */
    TEST_ASSERT_EQ(sim_qos_dscp_map[56], 7);   /* CS7  → Q7 */

    /* 未映射的 DSCP 落入 Q0（Best Effort） */
    TEST_ASSERT_EQ(sim_qos_dscp_map[1],  0);
    TEST_ASSERT_EQ(sim_qos_dscp_map[63], 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-QOS-2: 64 条 DSCP TCAM 规则安装
// ─────────────────────────────────────────────
void test_qos_dscp_tcam_rules(void) {
    TEST_BEGIN("QOS-2 : qos_init() installs 64 DSCP TCAM rules in stage 5");

    sim_hal_reset();
    qos_init();

    int cnt = sim_tcam_count_stage(TABLE_DSCP_MAP_STAGE);
    TEST_ASSERT_EQ(cnt, 64);

    /* 每个 DSCP 值都有对应规则 */
    for (int d = 0; d < 64; d++) {
        sim_tcam_rec_t *r = sim_tcam_find(TABLE_DSCP_MAP_STAGE,
                                           (uint16_t)(TABLE_DSCP_MAP_BASE + d));
        TEST_ASSERT_NOTNULL(r);
        if (!r) continue;
        TEST_ASSERT_EQ(r->entry.action_id, ACTION_SET_PRIO);
    }

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-QOS-3: DSCP TCAM 规则键值编码
// ─────────────────────────────────────────────
void test_qos_dscp_rule_encoding(void) {
    TEST_BEGIN("QOS-3 : DSCP TCAM key = dscp<<2, mask = 0xFC");

    sim_hal_reset();
    qos_init();

    /* DSCP=46 (EF): key=0xB8, mask=0xFC, action_params[0]=5 */
    sim_tcam_rec_t *r46 = sim_tcam_find(TABLE_DSCP_MAP_STAGE,
                                         TABLE_DSCP_MAP_BASE + 46);
    TEST_ASSERT_NOTNULL(r46);
    TEST_ASSERT_EQ(r46->entry.key.bytes[0],    (uint8_t)(46 << 2));  /* 0xB8 */
    TEST_ASSERT_EQ(r46->entry.mask.bytes[0],   0xFC);
    TEST_ASSERT_EQ(r46->entry.action_params[0], 5);

    /* DSCP=0 (BE): key=0x00, mask=0xFC, action_params[0]=0 */
    sim_tcam_rec_t *r0 = sim_tcam_find(TABLE_DSCP_MAP_STAGE,
                                        TABLE_DSCP_MAP_BASE + 0);
    TEST_ASSERT_NOTNULL(r0);
    TEST_ASSERT_EQ(r0->entry.key.bytes[0],    0x00);
    TEST_ASSERT_EQ(r0->entry.mask.bytes[0],   0xFC);
    TEST_ASSERT_EQ(r0->entry.action_params[0], 0);

    /* DSCP=56 (CS7): key=0xE0, action_params[0]=7 */
    sim_tcam_rec_t *r56 = sim_tcam_find(TABLE_DSCP_MAP_STAGE,
                                         TABLE_DSCP_MAP_BASE + 56);
    TEST_ASSERT_NOTNULL(r56);
    TEST_ASSERT_EQ(r56->entry.key.bytes[0],    (uint8_t)(56 << 2));  /* 0xE0 */
    TEST_ASSERT_EQ(r56->entry.action_params[0], 7);

    /* 验证 key 长度 = 1 */
    TEST_ASSERT_EQ(r46->entry.key.key_len,  1);
    TEST_ASSERT_EQ(r46->entry.mask.key_len, 1);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-QOS-4: DWRR 权重 CSR 写入
// ─────────────────────────────────────────────
void test_qos_dwrr_weights(void) {
    TEST_BEGIN("QOS-4 : DWRR weight set / update writes to CSR");

    sim_hal_reset();
    qos_init();

    /* 默认权重 = QOS_DEFAULT_WEIGHT（1500）*/
    for (int q = 0; q < 8; q++)
        TEST_ASSERT_EQ(sim_qos_dwrr[0][q], (uint32_t)QOS_DEFAULT_WEIGHT);

    /* 为 port 5 设置自定义权重 */
    uint32_t w[8] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
    TEST_ASSERT_OK(qos_port_set_weights(5, w));
    for (int q = 0; q < 8; q++)
        TEST_ASSERT_EQ(sim_qos_dwrr[5][q], w[q]);

    /* 其他端口不受影响 */
    TEST_ASSERT_EQ(sim_qos_dwrr[0][0], (uint32_t)QOS_DEFAULT_WEIGHT);

    /* 单队列更新：qos_dscp_set 内部调用 hal_qos_dscp_map_set */
    TEST_ASSERT_OK(qos_dscp_set(10, 3));
    TEST_ASSERT_EQ(sim_qos_dscp_map[10], 3);

    /* 无效参数 */
    TEST_ASSERT_NE(qos_port_set_weights(32, w), HAL_OK); /* port >= 32 */
    TEST_ASSERT_NE(qos_dscp_set(64, 0),         HAL_OK); /* dscp >= 64 */
    TEST_ASSERT_NE(qos_dscp_set(0,  8),         HAL_OK); /* queue >= 8 */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-QOS-5: PIR 速率 + 调度模式配置
// ─────────────────────────────────────────────
void test_qos_port_pir_mode(void) {
    TEST_BEGIN("QOS-5 : PIR rate and scheduler mode write to CSR");

    sim_hal_reset();
    qos_init();

    /* 设置 PIR */
    TEST_ASSERT_OK(qos_port_set_pir(3, 1000000000UL));
    TEST_ASSERT_EQ(sim_qos_pir[3], 1000000000UL);

    TEST_ASSERT_OK(qos_port_set_pir(31, 10000000000UL));
    TEST_ASSERT_EQ(sim_qos_pir[31], 10000000000UL);

    /* 调度模式 */
    TEST_ASSERT_OK(qos_port_set_mode(3, QOS_SCHED_SP_DWRR, 2));
    TEST_ASSERT_EQ(sim_qos_mode[3], QOS_SCHED_SP_DWRR);

    TEST_ASSERT_OK(qos_port_set_mode(0, QOS_SCHED_DWRR, 0));
    TEST_ASSERT_EQ(sim_qos_mode[0], QOS_SCHED_DWRR);

    /* 边界：无效端口 */
    TEST_ASSERT_NE(qos_port_set_pir(32, 1000), HAL_OK);
    /* 边界：无效调度模式 */
    TEST_ASSERT_NE(qos_port_set_mode(0, 99, 0), HAL_OK);
    /* 边界：sp_queues 过大 */
    TEST_ASSERT_NE(qos_port_set_mode(0, QOS_SCHED_SP_DWRR, 9), HAL_OK);

    TEST_END();
}
