// test_main.c
// 测试套件入口 — 运行所有用例，打印汇总报告

#include <stdio.h>
#include <stdlib.h>
#include "test_framework.h"
#include "sim_hal.h"

// 全局计数（test_framework.h 中声明为 extern）
int g_pass = 0;
int g_fail = 0;

// ─────────────────────────────────────────────
// 各模块测试函数声明
// ─────────────────────────────────────────────

/* VLAN */
void test_vlan_create(void);
void test_vlan_delete(void);
void test_vlan_port_access_ingress(void);
void test_vlan_port_trunk_ingress(void);
void test_vlan_port_add_egress(void);
void test_vlan_port_remove(void);

/* ARP */
void test_arp_punt_rule(void);
void test_arp_add_lookup_hit(void);
void test_arp_add_lookup_miss(void);
void test_arp_delete(void);
void test_arp_process_request(void);
void test_arp_process_reply(void);
void test_arp_age_cycle(void);

/* QoS */
void test_qos_dscp_default_map(void);
void test_qos_dscp_tcam_rules(void);
void test_qos_dscp_rule_encoding(void);
void test_qos_dwrr_weights(void);
void test_qos_port_pir_mode(void);

/* Route */
void test_route_add_del(void);
void test_route_host(void);
void test_route_default(void);

/* ACL */
void test_acl_deny(void);
void test_acl_permit(void);
void test_acl_delete(void);
void test_acl_seq_ids(void);

/* CLI */
void test_cli_unknown_cmd(void);
void test_cli_help(void);
void test_cli_route_add(void);
void test_cli_route_del(void);
void test_cli_acl_deny(void);
void test_cli_vlan_port(void);

// ─────────────────────────────────────────────
// main
// ─────────────────────────────────────────────

int main(void) {
    printf("RV-P4 Control Plane Unit Tests\n");
    printf("================================\n");

    // ── VLAN 测试套件 ────────────────────────
    TEST_SUITE("VLAN Management (6 cases)");
    test_vlan_create();
    test_vlan_delete();
    test_vlan_port_access_ingress();
    test_vlan_port_trunk_ingress();
    test_vlan_port_add_egress();
    test_vlan_port_remove();

    // ── ARP 测试套件 ─────────────────────────
    TEST_SUITE("ARP / Neighbor Table (7 cases)");
    test_arp_punt_rule();
    test_arp_add_lookup_hit();
    test_arp_add_lookup_miss();
    test_arp_delete();
    test_arp_process_request();
    test_arp_process_reply();
    test_arp_age_cycle();

    // ── QoS 测试套件 ─────────────────────────
    TEST_SUITE("QoS Scheduling (5 cases)");
    test_qos_dscp_default_map();
    test_qos_dscp_tcam_rules();
    test_qos_dscp_rule_encoding();
    test_qos_dwrr_weights();
    test_qos_port_pir_mode();

    // ── Route 测试套件 ────────────────────────
    TEST_SUITE("IPv4 Routing (3 cases)");
    test_route_add_del();
    test_route_host();
    test_route_default();

    // ── ACL 测试套件 ──────────────────────────
    TEST_SUITE("ACL Rules (4 cases)");
    test_acl_deny();
    test_acl_permit();
    test_acl_delete();
    test_acl_seq_ids();

    // ── CLI 测试套件 ──────────────────────────
    TEST_SUITE("CLI Commands (6 cases)");
    test_cli_unknown_cmd();
    test_cli_help();
    test_cli_route_add();
    test_cli_route_del();
    test_cli_acl_deny();
    test_cli_vlan_port();

    // ── 汇总 ─────────────────────────────────
    int total = g_pass + g_fail;
    printf("\n================================\n");
    printf("Results: %d/%d passed", g_pass, total);
    if (g_fail == 0)
        printf("  ✓ ALL PASS\n");
    else
        printf("  ✗ %d FAILED\n", g_fail);
    printf("================================\n");

    return (g_fail == 0) ? 0 : 1;
}
