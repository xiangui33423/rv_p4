// test_vlan.c
// VLAN 管理模块测试用例（6 个）
//
// 用例列表：
//   1. test_vlan_create        — 创建/重复创建/无效 ID
//   2. test_vlan_delete        — 删除时清除成员 bitmap 和出口规则
//   3. test_vlan_port_access_ingress — access 模式安装入口 TCAM 规则
//   4. test_vlan_port_trunk_ingress  — trunk 模式安装入口 TCAM 规则
//   5. test_vlan_port_add_egress     — 加入 VLAN 安装出口规则
//   6. test_vlan_port_remove         — 离开 VLAN 删除出口规则 + bitmap 清除

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "vlan.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// TC-VLAN-1: 创建 VLAN
// ─────────────────────────────────────────────
void test_vlan_create(void) {
    TEST_BEGIN("VLAN-1: create VLAN (valid / dup / invalid)");

    sim_hal_reset();

    /* 正常创建 */
    TEST_ASSERT_OK(vlan_create(10));
    TEST_ASSERT_OK(vlan_create(20));
    TEST_ASSERT_OK(vlan_create(255));

    /* CSR: 成员 bitmap 初始为 0 */
    TEST_ASSERT_EQ(sim_vlan_member[10],  0U);
    TEST_ASSERT_EQ(sim_vlan_member[20],  0U);
    TEST_ASSERT_EQ(sim_vlan_member[255], 0U);

    /* 重复创建返回 HAL_OK（幂等） */
    TEST_ASSERT_OK(vlan_create(10));

    /* 无效 ID */
    TEST_ASSERT_NE(vlan_create(0),   HAL_OK);
    TEST_ASSERT_NE(vlan_create(256), HAL_OK);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-VLAN-2: 删除 VLAN
// ─────────────────────────────────────────────
void test_vlan_delete(void) {
    TEST_BEGIN("VLAN-2: delete VLAN clears bitmap + egress rules");

    sim_hal_reset();
    vlan_create(10);
    vlan_port_add(10, 0, 0);   // port 0, untagged
    vlan_port_add(10, 1, 0);   // port 1, untagged
    vlan_port_add(10, 2, 1);   // port 2, tagged

    /* 创建后有 3 条出口规则 */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 3);
    TEST_ASSERT_EQ(sim_vlan_member[10], (1U<<0)|(1U<<1)|(1U<<2));

    vlan_delete(10);

    /* bitmap 清零 */
    TEST_ASSERT_EQ(sim_vlan_member[10],   0U);
    TEST_ASSERT_EQ(sim_vlan_untagged[10], 0U);

    /* 出口规则全部标记删除 */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 0);
    TEST_ASSERT_NULL(sim_tcam_find(TABLE_VLAN_EGRESS_STAGE,
                                   (uint16_t)VLAN_EGRESS_ENTRY(0, 10)));

    /* 删除不存在的 VLAN 返回错误 */
    TEST_ASSERT_NE(vlan_delete(10), HAL_OK);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-VLAN-3: access 模式入口规则
// ─────────────────────────────────────────────
void test_vlan_port_access_ingress(void) {
    TEST_BEGIN("VLAN-3: access port installs 2 ingress TCAM rules");

    sim_hal_reset();
    vlan_create(10);

    /* 配置 port 3 为 access, PVID=10 */
    TEST_ASSERT_OK(vlan_port_set_pvid(3, 10));
    TEST_ASSERT_OK(vlan_port_set_mode(3, VLAN_MODE_ACCESS));

    /* CSR 写入验证 */
    TEST_ASSERT_EQ(sim_vlan_pvid[3], 10);
    TEST_ASSERT_EQ(sim_vlan_mode[3], VLAN_MODE_ACCESS);

    /* stage 4 应有 2 条规则（无标签帧 + 带 PVID 标签帧） */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_INGRESS_STAGE), 2);

    /* 规则 0：无标签帧 → ACTION_VLAN_ASSIGN_PVID */
    sim_tcam_rec_t *r0 = sim_tcam_find(TABLE_VLAN_INGRESS_STAGE,
                                        (uint16_t)VLAN_INGRESS_ENTRY(3, 0));
    TEST_ASSERT_NOTNULL(r0);
    TEST_ASSERT_EQ(r0->entry.action_id, ACTION_VLAN_ASSIGN_PVID);
    TEST_ASSERT_EQ(r0->entry.key.bytes[0], 3);   /* port */
    /* 动作参数：meta.vlan_id = pvid = 10 (big-endian 2B) */
    uint16_t pvid_param = (uint16_t)((r0->entry.action_params[0] << 8) |
                                      r0->entry.action_params[1]);
    TEST_ASSERT_EQ(pvid_param, 10);

    /* 规则 1：带 PVID 标签帧 → ACTION_VLAN_ACCEPT_TAGGED */
    sim_tcam_rec_t *r1 = sim_tcam_find(TABLE_VLAN_INGRESS_STAGE,
                                        (uint16_t)VLAN_INGRESS_ENTRY(3, 1));
    TEST_ASSERT_NOTNULL(r1);
    TEST_ASSERT_EQ(r1->entry.action_id, ACTION_VLAN_ACCEPT_TAGGED);
    TEST_ASSERT_EQ(r1->entry.key.bytes[0], 3);
    /* 掩码仅匹配 VID（低 12 位）：u16_to_key 大端写 0x0FFF → bytes[1]=0x0F, bytes[2]=0xFF */
    TEST_ASSERT_EQ(r1->entry.mask.bytes[1], 0x0F);
    TEST_ASSERT_EQ(r1->entry.mask.bytes[2], 0xFF);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-VLAN-4: trunk 模式入口规则
// ─────────────────────────────────────────────
void test_vlan_port_trunk_ingress(void) {
    TEST_BEGIN("VLAN-4: trunk port installs 2 ingress TCAM rules");

    sim_hal_reset();
    vlan_create(10);

    TEST_ASSERT_OK(vlan_port_set_pvid(5, 10));
    TEST_ASSERT_OK(vlan_port_set_mode(5, VLAN_MODE_TRUNK));

    TEST_ASSERT_EQ(sim_vlan_mode[5], VLAN_MODE_TRUNK);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_INGRESS_STAGE), 2);

    /* 规则 0：无标签帧 → 赋 PVID */
    sim_tcam_rec_t *r0 = sim_tcam_find(TABLE_VLAN_INGRESS_STAGE,
                                        (uint16_t)VLAN_INGRESS_ENTRY(5, 0));
    TEST_ASSERT_NOTNULL(r0);
    TEST_ASSERT_EQ(r0->entry.action_id, ACTION_VLAN_ASSIGN_PVID);

    /* 规则 1：任意带标签帧 → 接受（掩码全 0，通配） */
    sim_tcam_rec_t *r1 = sim_tcam_find(TABLE_VLAN_INGRESS_STAGE,
                                        (uint16_t)VLAN_INGRESS_ENTRY(5, 1));
    TEST_ASSERT_NOTNULL(r1);
    TEST_ASSERT_EQ(r1->entry.action_id, ACTION_VLAN_ACCEPT_TAGGED);
    /* trunk 通配：vlan_tci mask = 0x0000 */
    TEST_ASSERT_EQ(r1->entry.mask.bytes[1], 0x00);
    TEST_ASSERT_EQ(r1->entry.mask.bytes[2], 0x00);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-VLAN-5: 端口加入 VLAN 安装出口规则
// ─────────────────────────────────────────────
void test_vlan_port_add_egress(void) {
    TEST_BEGIN("VLAN-5: port_add installs egress rule + updates bitmap");

    sim_hal_reset();
    vlan_create(30);

    /* port 7 加入 VLAN 30（无标签，access） */
    TEST_ASSERT_OK(vlan_port_add(30, 7, 0));

    /* bitmap bit[7] 置位 */
    TEST_ASSERT_NE(sim_vlan_member[30]   & (1U << 7), 0U);
    TEST_ASSERT_NE(sim_vlan_untagged[30] & (1U << 7), 0U);

    /* 出口规则：stage 6，key=[port=7, vlan=30]，action=STRIP_TAG */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_VLAN_EGRESS_STAGE,
                                       (uint16_t)VLAN_EGRESS_ENTRY(7, 30));
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id,   ACTION_VLAN_STRIP_TAG);
    TEST_ASSERT_EQ(r->entry.key.bytes[0], 7);    /* eg_port */
    TEST_ASSERT_EQ(r->entry.key.bytes[1], 30);   /* vlan_id */

    /* port 8 加入 VLAN 30（带标签，trunk）*/
    TEST_ASSERT_OK(vlan_port_add(30, 8, 1));
    sim_tcam_rec_t *r2 = sim_tcam_find(TABLE_VLAN_EGRESS_STAGE,
                                        (uint16_t)VLAN_EGRESS_ENTRY(8, 30));
    TEST_ASSERT_NOTNULL(r2);
    TEST_ASSERT_EQ(r2->entry.action_id, ACTION_VLAN_KEEP_TAG);

    /* 无效参数 */
    TEST_ASSERT_NE(vlan_port_add(30, 32, 0), HAL_OK);  /* port >= 32 */
    TEST_ASSERT_NE(vlan_port_add(0,  0,  0), HAL_OK);  /* VLAN 0 未创建 */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-VLAN-6: 端口离开 VLAN
// ─────────────────────────────────────────────
void test_vlan_port_remove(void) {
    TEST_BEGIN("VLAN-6: port_remove deletes egress rule + clears bitmap");

    sim_hal_reset();
    vlan_create(40);
    vlan_port_add(40, 2, 0);
    vlan_port_add(40, 4, 0);

    /* 加入后 2 条出口规则 */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 2);

    /* 移除 port 2 */
    TEST_ASSERT_OK(vlan_port_remove(40, 2));

    /* bitmap bit[2] 清零，bit[4] 保留 */
    TEST_ASSERT_EQ(sim_vlan_member[40] & (1U << 2), 0U);
    TEST_ASSERT_NE(sim_vlan_member[40] & (1U << 4), 0U);

    /* 出口规则：port 2 已删除，port 4 保留 */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 1);
    TEST_ASSERT_NULL(sim_tcam_find(TABLE_VLAN_EGRESS_STAGE,
                                    (uint16_t)VLAN_EGRESS_ENTRY(2, 40)));
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_VLAN_EGRESS_STAGE,
                                       (uint16_t)VLAN_EGRESS_ENTRY(4, 40)));

    TEST_END();
}
