// cp_main.c
// 控制面固件主文件
// 初始化所有模块，主循环处理 Punt RX 包 + CLI 轮询 + 定时任务

#include "rv_p4_hal.h"
#include "table_map.h"
#include "vlan.h"
#include "arp.h"
#include "qos.h"
#include "fdb.h"
#include "route.h"
#include "acl.h"
#include "cli.h"

#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

static void print_port_stats(uint8_t port) {
    port_stats_t s;
    if (hal_port_stats(port, &s) == HAL_OK) {
        printf("Port %2d: rx_pkts=%llu rx_bytes=%llu "
               "tx_pkts=%llu tx_bytes=%llu\n",
               port,
               (unsigned long long)s.rx_pkts,
               (unsigned long long)s.rx_bytes,
               (unsigned long long)s.tx_pkts,
               (unsigned long long)s.tx_bytes);
    }
}

// ─────────────────────────────────────────────
// 主函数
// ─────────────────────────────────────────────

int main(void) {
    int ret;

    // ── 初始化 HAL ──────────────────────────────
    ret = hal_init();
    if (ret != HAL_OK) {
        printf("HAL init failed: %d\n", ret);
        return 1;
    }

    // 使能所有端口
    for (int p = 0; p < 32; p++)
        hal_port_enable((port_id_t)p);

    // ── VLAN 初始化 ─────────────────────────────
    vlan_init();
    vlan_create(10);
    vlan_create(20);

    for (int p = 0; p <= 7; p++) {
        vlan_port_set_pvid((port_id_t)p, 10);
        vlan_port_set_mode((port_id_t)p, VLAN_MODE_ACCESS);
        vlan_port_add(10, (port_id_t)p, 0);
    }
    for (int p = 8; p <= 15; p++) {
        vlan_port_set_pvid((port_id_t)p, 20);
        vlan_port_set_mode((port_id_t)p, VLAN_MODE_ACCESS);
        vlan_port_add(20, (port_id_t)p, 0);
    }
    vlan_port_set_mode(31, VLAN_MODE_TRUNK);
    vlan_port_add(10, 31, 1);
    vlan_port_add(20, 31, 1);
    printf("VLAN init done\n");

    // ── ARP 初始化 ──────────────────────────────
    arp_init();
    static const uint8_t mac_p0[6] = {0x02,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t mac_p8[6] = {0x02,0x00,0x00,0x00,0x00,0x08};
    arp_set_port_intf(0,  0x0A0A0001, mac_p0);
    arp_set_port_intf(8,  0x0A140101, mac_p8);

    static const uint8_t gw_mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    arp_add(0x0A0A0002, gw_mac, 0, 10);
    printf("ARP init done\n");

    // ── QoS 初始化 ──────────────────────────────
    qos_init();
    uint32_t uplink_weights[8] = {1500, 3000, 3000, 6000, 6000, 12000, 0, 0};
    qos_port_set_weights(31, uplink_weights);
    qos_port_set_mode(31, QOS_SCHED_SP_DWRR, 2);
    qos_port_set_pir(31, 10000000000ULL);
    for (int p = 0; p <= 7; p++)
        qos_port_set_pir((port_id_t)p, 1000000000ULL);
    printf("QoS init done\n");

    // ── FDB 初始化 ──────────────────────────────
    fdb_init();
    fdb_add_static(0x001122334455ULL, 0, 10);
    fdb_add_static(0x001122334466ULL, 8, 20);

    // ── 路由表初始化 ────────────────────────────
    route_init();

    /* 默认路由：直接写 TCAM（ACTION_DROP，route_add 仅支持 FORWARD） */
    {
        tcam_entry_t e;
        memset(&e, 0, sizeof(e));
        e.key.key_len  = 4;
        e.mask.key_len = 4;
        e.stage     = TABLE_IPV4_LPM_STAGE;
        e.table_id  = TABLE_IPV4_LPM_BASE + 0xFFFF;
        e.action_id = ACTION_DROP;
        hal_tcam_insert(&e);
    }

    route_add(0x0A0A0000, 16, 0,  0x001122334455ULL);  // 10.10.0.0/16 → port 0
    route_add(0x0A140000, 16, 8,  0x001122334466ULL);  // 10.20.0.0/16 → port 8
    route_add(0x0A010100, 24, 2,  0x001122334477ULL);  // 10.1.1.0/24  → port 2

    // ── ACL 初始化 ──────────────────────────────
    acl_init();
    acl_add_deny(0xC0A80000, 0xFFFF0000, 0x00000000, 0x00000000, 80);

    printf("Initial config done\n");

    // ── CLI 初始化 ──────────────────────────────
    cli_init();

    // ── 主循环 ────────────────────────────────
    uint32_t tick     = 0;
    uint32_t sec_tick = 0;

    while (1) {
        /* 简化延时（实际应使用定时器中断） */
        for (volatile int i = 0; i < 100000; i++);

        /* ── 处理 Punt RX 包 ─────────────────── */
        punt_pkt_t pkt;
        while (hal_punt_rx_poll(&pkt) == HAL_OK) {
            if (pkt.reason == PUNT_REASON_ARP)
                arp_process_pkt(&pkt);
        }

        /* ── CLI 轮询 ────────────────────────── */
        cli_poll();

        tick++;

        /* ── 1 秒周期任务 ────────────────────── */
        if (tick % 10 == 0) {
            sec_tick++;
            arp_age(sec_tick);
            fdb_age(sec_tick);

            /* ── 60 秒周期：打印统计 ─────────── */
            if (sec_tick % 60 == 0) {
                printf("=== Port Stats (t=%us) ===\n", sec_tick);
                for (int p = 0; p < 4; p++)
                    print_port_stats((uint8_t)p);
            }
        }
    }

    return 0;
}
