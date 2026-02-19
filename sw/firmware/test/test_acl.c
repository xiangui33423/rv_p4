// test_acl.c
// ACL 模块测试用例（4 个）
//
//   1. test_acl_deny         — deny 规则安装 ACTION_DENY + 返回 rule_id
//   2. test_acl_permit       — permit 规则安装 ACTION_PERMIT
//   3. test_acl_delete       — del 撤销 TCAM 条目
//   4. test_acl_seq_ids      — 多条规则按序分配 ID，支持独立删除

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "acl.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// TC-ACL-1: deny 规则
// ─────────────────────────────────────────────
void test_acl_deny(void) {
    TEST_BEGIN("ACL-1 : acl_add_deny installs ACTION_DENY TCAM entry");

    sim_hal_reset();
    acl_init();

    /* 拒绝 192.168.0.0/16 访问 10.0.0.0/8, TCP 80 */
    int id = acl_add_deny(0xC0A80000u, 0xFFFF0000u,
                          0x0A000000u, 0xFF000000u, 80);
    TEST_ASSERT(id >= 0);

    /* rule_id=0 → table_id = TABLE_ACL_INGRESS_BASE + 0 = 0 */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                      TABLE_ACL_INGRESS_BASE + (uint16_t)id);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id, ACTION_DENY);

    /* src_ip key 大端：192.168 → 0xC0 0xA8 0x00 0x00 */
    TEST_ASSERT_EQ(r->entry.key.bytes[0], 0xC0);
    TEST_ASSERT_EQ(r->entry.key.bytes[1], 0xA8);

    /* dport key：bytes[8..9] = 0x00 0x50 (80) */
    TEST_ASSERT_EQ(r->entry.key.bytes[8], 0x00);
    TEST_ASSERT_EQ(r->entry.key.bytes[9], 0x50);

    /* dport mask = 0xFF 0xFF（精确匹配） */
    TEST_ASSERT_EQ(r->entry.mask.bytes[8], 0xFF);
    TEST_ASSERT_EQ(r->entry.mask.bytes[9], 0xFF);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ACL-2: permit 规则
// ─────────────────────────────────────────────
void test_acl_permit(void) {
    TEST_BEGIN("ACL-2 : acl_add_permit installs ACTION_PERMIT TCAM entry");

    sim_hal_reset();
    acl_init();

    /* 放行 10.10.0.0/16 → 0.0.0.0/0 */
    int id = acl_add_permit(0x0A0A0000u, 0xFFFF0000u, 0u, 0u);
    TEST_ASSERT(id >= 0);

    sim_tcam_rec_t *r = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                      TABLE_ACL_INGRESS_BASE + (uint16_t)id);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id, ACTION_PERMIT);
    TEST_ASSERT_EQ(r->entry.key.key_len, 8);   /* 仅 src+dst IP，无端口 */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ACL-3: 删除规则
// ─────────────────────────────────────────────
void test_acl_delete(void) {
    TEST_BEGIN("ACL-3 : acl_delete removes TCAM entry");

    sim_hal_reset();
    acl_init();

    int id = acl_add_deny(0x01010101u, 0xFFFFFFFFu, 0u, 0u, 0u);
    TEST_ASSERT(id >= 0);

    /* 删除前存在 */
    sim_tcam_rec_t *r1 = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                        TABLE_ACL_INGRESS_BASE + (uint16_t)id);
    TEST_ASSERT_NOTNULL(r1);

    TEST_ASSERT_OK(acl_delete((uint16_t)id));

    /* 删除后消失 */
    sim_tcam_rec_t *r2 = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                        TABLE_ACL_INGRESS_BASE + (uint16_t)id);
    TEST_ASSERT(r2 == NULL);

    /* 删除不存在的 ID 返回错误 */
    TEST_ASSERT_NE(acl_delete(99u), HAL_OK);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ACL-4: 多条规则顺序 ID + 独立删除
// ─────────────────────────────────────────────
void test_acl_seq_ids(void) {
    TEST_BEGIN("ACL-4 : multiple rules get sequential IDs; delete one leaves others");

    sim_hal_reset();
    acl_init();

    int id0 = acl_add_deny(  0xC0A80000u, 0xFFFF0000u, 0u, 0u, 0u);
    int id1 = acl_add_permit(0x0A000000u, 0xFF000000u, 0u, 0u);
    int id2 = acl_add_deny(  0xAC100000u, 0xFFF00000u, 0u, 0u, 443u);
    TEST_ASSERT(id0 >= 0 && id1 > id0 && id2 > id1);

    /* 删除中间规则 id1 */
    TEST_ASSERT_OK(acl_delete((uint16_t)id1));

    /* id0 和 id2 仍然存在 */
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                      TABLE_ACL_INGRESS_BASE + (uint16_t)id0));
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                      TABLE_ACL_INGRESS_BASE + (uint16_t)id2));
    /* id1 已删除 */
    TEST_ASSERT(sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                              TABLE_ACL_INGRESS_BASE + (uint16_t)id1) == NULL);

    TEST_END();
}
