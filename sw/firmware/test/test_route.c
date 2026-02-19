// test_route.c
// 路由表模块测试用例（3 个）
//
//   1. test_route_add_del     — add 安装 TCAM 规则，del 撤销
//   2. test_route_host        — /32 主机路由编码正确
//   3. test_route_default     — /0 默认路由（0.0.0.0/0）边界处理

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "route.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// TC-ROUTE-1: add 后 TCAM 存在，del 后消失
// ─────────────────────────────────────────────
void test_route_add_del(void) {
    TEST_BEGIN("ROUTE-1: route_add installs TCAM entry; route_del removes it");

    sim_hal_reset();
    route_init();

    /* 10.0.0.0/8 → port 2, next-hop aa:bb:cc:dd:ee:ff */
    uint64_t dmac = 0xAABBCCDDEEFFULL;
    TEST_ASSERT_OK(route_add(0x0A000000u, 8, 2, dmac));

    /* TCAM 中的 table_id = TABLE_IPV4_LPM_BASE + (0x0A000000 >> 24) = 0x0A = 10 */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id,         ACTION_FORWARD);
    TEST_ASSERT_EQ(r->entry.action_params[0],  2);      /* port */
    TEST_ASSERT_EQ(r->entry.action_params[1],  0xAA);   /* dmac[0] */
    TEST_ASSERT_EQ(r->entry.action_params[6],  0xFF);   /* dmac[5] */

    /* key = 10.0.0.0 大端 */
    TEST_ASSERT_EQ(r->entry.key.bytes[0], 0x0A);
    TEST_ASSERT_EQ(r->entry.key.bytes[1], 0x00);
    TEST_ASSERT_EQ(r->entry.key.key_len,  4);

    /* mask = /8 → 0xFF000000 大端 */
    TEST_ASSERT_EQ(r->entry.mask.bytes[0], 0xFF);
    TEST_ASSERT_EQ(r->entry.mask.bytes[1], 0x00);

    /* 删除后条目被标记为 deleted */
    TEST_ASSERT_OK(route_del(0x0A000000u, 8));
    sim_tcam_rec_t *r2 = sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u);
    TEST_ASSERT(r2 == NULL);   /* deleted 后 find 返回 NULL */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ROUTE-2: /32 主机路由
// ─────────────────────────────────────────────
void test_route_host(void) {
    TEST_BEGIN("ROUTE-2: /32 host route has correct table_id and full mask");

    sim_hal_reset();
    route_init();

    /* 192.168.1.1/32 → port 5 */
    TEST_ASSERT_OK(route_add(0xC0A80101u, 32, 5, 0x001122334455ULL));

    /* table_id = TABLE_IPV4_LPM_BASE + (0xC0A80101 >> 0) = 0xC0A80101 的低 16 位 */
    /* = 0xC0A80101 >> (32-32) = 0xC0A80101 */
    /* 但 table_id 是 uint16_t，所以截断为 0x0101 */
    uint16_t expected_tid = (uint16_t)(TABLE_IPV4_LPM_BASE + 0x0101u);
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, expected_tid);
    TEST_ASSERT_NOTNULL(r);

    /* mask = /32 → 全 0xFF */
    TEST_ASSERT_EQ(r->entry.mask.bytes[0], 0xFF);
    TEST_ASSERT_EQ(r->entry.mask.bytes[3], 0xFF);
    TEST_ASSERT_EQ(r->entry.action_params[0], 5);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ROUTE-3: 默认路由 0.0.0.0/0
// ─────────────────────────────────────────────
void test_route_default(void) {
    TEST_BEGIN("ROUTE-3: 0.0.0.0/0 default route (len=0 edge case)");

    sim_hal_reset();
    route_init();

    TEST_ASSERT_OK(route_add(0x00000000u, 0, 7, 0x0011223344FFull));

    /* table_id = TABLE_IPV4_LPM_BASE + 0 = 0 */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, TABLE_IPV4_LPM_BASE);
    TEST_ASSERT_NOTNULL(r);

    /* mask = /0 → 全 0x00（通配） */
    TEST_ASSERT_EQ(r->entry.mask.bytes[0], 0x00);
    TEST_ASSERT_EQ(r->entry.mask.bytes[3], 0x00);

    /* len > 32 应返回错误 */
    TEST_ASSERT_NE(route_add(0x0u, 33, 0, 0), HAL_OK);

    TEST_END();
}
