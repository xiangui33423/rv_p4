// test_cli.c
// CLI 命令分发模块测试用例（6 个）
//
//   1. test_cli_unknown_cmd  — 未知命令返回 0
//   2. test_cli_help         — help 命令返回 1（不崩溃）
//   3. test_cli_route_add    — route add 安装 TCAM 路由条目
//   4. test_cli_route_del    — route del 撤销条目
//   5. test_cli_acl_deny     — acl deny 安装 ACL TCAM 条目
//   6. test_cli_vlan_port    — vlan create + port add 安装出口 TCAM

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "cli_cmds.h"
#include "vlan.h"
#include "route.h"
#include "acl.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// TC-CLI-1: 未知命令
// ─────────────────────────────────────────────
void test_cli_unknown_cmd(void) {
    TEST_BEGIN("CLI-1 : unknown command returns 0");

    sim_hal_reset();

    char *argv[] = {"boguscmd"};
    TEST_ASSERT_EQ(cli_exec_cmd(1, argv), 0);

    char *argv2[] = {"foo", "bar", "baz"};
    TEST_ASSERT_EQ(cli_exec_cmd(3, argv2), 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-CLI-2: help 命令
// ─────────────────────────────────────────────
void test_cli_help(void) {
    TEST_BEGIN("CLI-2 : help command recognized (returns 1)");

    sim_hal_reset();

    char *argv[] = {"help"};
    TEST_ASSERT_EQ(cli_exec_cmd(1, argv), 1);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-CLI-3: route add
// ─────────────────────────────────────────────
void test_cli_route_add(void) {
    TEST_BEGIN("CLI-3 : 'route add 10.0.0.0/8 2 aa:bb:cc:dd:ee:ff' installs TCAM");

    sim_hal_reset();
    route_init();

    char *argv[] = {"route", "add", "10.0.0.0/8", "2", "aa:bb:cc:dd:ee:ff"};
    TEST_ASSERT_EQ(cli_exec_cmd(5, argv), 1);

    /* 10.0.0.0/8 → table_id = TABLE_IPV4_LPM_BASE + (0x0A000000 >> 24) = 10 */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id,         ACTION_FORWARD);
    TEST_ASSERT_EQ(r->entry.action_params[0],  2);      /* port */
    TEST_ASSERT_EQ(r->entry.action_params[1],  0xAA);   /* mac[0] */
    TEST_ASSERT_EQ(r->entry.action_params[6],  0xFF);   /* mac[5] */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-CLI-4: route del
// ─────────────────────────────────────────────
void test_cli_route_del(void) {
    TEST_BEGIN("CLI-4 : 'route del 10.0.0.0/8' removes TCAM entry");

    sim_hal_reset();
    route_init();

    char *add_argv[] = {"route", "add", "10.0.0.0/8", "3", "11:22:33:44:55:66"};
    cli_exec_cmd(5, add_argv);

    /* 确认存在 */
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u));

    char *del_argv[] = {"route", "del", "10.0.0.0/8"};
    TEST_ASSERT_EQ(cli_exec_cmd(3, del_argv), 1);

    /* 应已删除 */
    TEST_ASSERT(sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u) == NULL);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-CLI-5: acl deny
// ─────────────────────────────────────────────
void test_cli_acl_deny(void) {
    TEST_BEGIN("CLI-5 : 'acl deny 192.168.0.0/16 0.0.0.0/0 80' installs ACTION_DENY");

    sim_hal_reset();
    acl_init();

    char *argv[] = {"acl", "deny", "192.168.0.0/16", "0.0.0.0/0", "80"};
    TEST_ASSERT_EQ(cli_exec_cmd(5, argv), 1);

    /* 第一条规则 rule_id=0 → table_id = TABLE_ACL_INGRESS_BASE + 0 */
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                      TABLE_ACL_INGRESS_BASE);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id, ACTION_DENY);

    /* src key: 192.168.0.0 大端 */
    TEST_ASSERT_EQ(r->entry.key.bytes[0], 0xC0);
    TEST_ASSERT_EQ(r->entry.key.bytes[1], 0xA8);

    /* dport key bytes[8..9] = 0x00 0x50 */
    TEST_ASSERT_EQ(r->entry.key.bytes[9], 80);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-CLI-6: vlan create + port add
// ─────────────────────────────────────────────
void test_cli_vlan_port(void) {
    TEST_BEGIN("CLI-6 : 'vlan create 50' + 'vlan port 50 add 3 untagged' → egress TCAM");

    sim_hal_reset();
    vlan_init();   /* 重置 VLAN 软件状态 */
    sim_hal_reset(); /* 再次清空 TCAM，去掉 vlan_init() 产生的默认规则 */

    char *create_argv[] = {"vlan", "create", "50"};
    TEST_ASSERT_EQ(cli_exec_cmd(3, create_argv), 1);

    char *add_argv[] = {"vlan", "port", "50", "add", "3", "untagged"};
    TEST_ASSERT_EQ(cli_exec_cmd(6, add_argv), 1);

    /* egress table_id = VLAN_EGRESS_ENTRY(3, 50) = 3*256 + 50 = 818 */
    uint16_t tid = (uint16_t)(3u * 256u + 50u);
    sim_tcam_rec_t *r = sim_tcam_find(TABLE_VLAN_EGRESS_STAGE, tid);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id, ACTION_VLAN_STRIP_TAG);  /* untagged 出口 */

    TEST_END();
}
