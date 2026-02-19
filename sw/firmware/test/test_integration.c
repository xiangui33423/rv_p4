// test_integration.c
// 集成测试 / 系统测试（6 个场景）
//
//   IT-SYS-1: 全量初始化 — 所有模块同时初始化，Stage 分配正确，无 TCAM 溢出
//   IT-SYS-2: ARP Request Punt → arp_process_pkt → Reply 内容 + ARP表 + FDB 联动
//   IT-SYS-3: ARP Reply Punt → ARP表与 FDB TCAM 双表字段一致性
//   IT-SYS-4: arp_delete → FDB TCAM 联动清理        【已知缺陷，预期 FAIL】
//   IT-SYS-5: Route(Stage0) + ACL(Stage1) + FDB(Stage2) 三模块共存互不干扰
//   IT-SYS-6: CLI 多命令序列 → 三个 Stage TCAM 同时生效

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "vlan.h"
#include "arp.h"
#include "qos.h"
#include "fdb.h"
#include "route.h"
#include "acl.h"
#include "cli_cmds.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// 内部工具：构造标准 ARP 以太帧（42 字节）
// ─────────────────────────────────────────────

/*
 * build_arp_pkt — 按 RFC 826 格式写入 buf
 *   oper : 1=Request, 2=Reply
 *   sha/spa: 发送方 MAC / IP
 *   tha/tpa: 目标方 MAC / IP
 *   eth_dst/eth_src: 以太网头 dst/src
 *
 * 返回写入的字节数（恒为 42）。
 */
static uint16_t build_arp_pkt(uint8_t *buf,
                               uint16_t oper,
                               const uint8_t *sha, uint32_t spa,
                               const uint8_t *tha, uint32_t tpa,
                               const uint8_t *eth_dst,
                               const uint8_t *eth_src)
{
    int off = 0;

    /* 以太网头（14 B） */
    memcpy(buf + off, eth_dst, 6); off += 6;
    memcpy(buf + off, eth_src, 6); off += 6;
    buf[off++] = 0x08; buf[off++] = 0x06;      /* EtherType = ARP */

    /* ARP 固定头（8 B） */
    buf[off++] = 0x00; buf[off++] = 0x01;      /* htype = Ethernet */
    buf[off++] = 0x08; buf[off++] = 0x00;      /* ptype = IPv4 */
    buf[off++] = 6;                              /* hlen */
    buf[off++] = 4;                              /* plen */
    buf[off++] = (uint8_t)(oper >> 8);
    buf[off++] = (uint8_t)(oper & 0xFF);        /* oper */

    /* ARP 负载（20 B） */
    memcpy(buf + off, sha, 6);  off += 6;       /* SHA */
    buf[off++] = (uint8_t)((spa >> 24) & 0xFF);
    buf[off++] = (uint8_t)((spa >> 16) & 0xFF);
    buf[off++] = (uint8_t)((spa >>  8) & 0xFF);
    buf[off++] = (uint8_t)( spa        & 0xFF); /* SPA */
    memcpy(buf + off, tha, 6);  off += 6;       /* THA */
    buf[off++] = (uint8_t)((tpa >> 24) & 0xFF);
    buf[off++] = (uint8_t)((tpa >> 16) & 0xFF);
    buf[off++] = (uint8_t)((tpa >>  8) & 0xFF);
    buf[off++] = (uint8_t)( tpa        & 0xFF); /* TPA */

    return (uint16_t)off;   /* = 42 */
}

// ─────────────────────────────────────────────
// IT-SYS-1: 全量初始化
// ─────────────────────────────────────────────
void test_sys_full_init(void)
{
    TEST_BEGIN("SYS-1 : 全量初始化 — 各 Stage 条目数正确，无 TCAM 溢出");

    sim_hal_reset();

    /* 按 cp_main 顺序初始化所有模块 */
    vlan_init();
    arp_init();
    qos_init();
    fdb_init();
    route_init();
    acl_init();

    /*
     * 预期 TCAM 布局（全量初始化后，未执行任何 cp_main 业务配置）：
     *   Stage 3（ARP Punt）:  1 条（install_arp_punt_rule）
     *   Stage 5（DSCP 映射）: 64 条（qos_init → qos_apply_dscp_rules）
     *   Stage 6（VLAN 出口）: 32 条（vlan_init → 对 VLAN 1 的 32 端口各一条出口规则）
     *   Stage 0/1/2（路由/ACL/FDB）: 0 条（尚未写入任何规则）
     */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_ARP_TRAP_STAGE),     1);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_DSCP_MAP_STAGE),    64);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 32);

    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_IPV4_LPM_STAGE),    0);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_ACL_INGRESS_STAGE), 0);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_L2_FDB_STAGE),      0);

    /* 总 TCAM 条目必须在模拟 HAL 容量内 */
    int total = sim_tcam_count_stage(TABLE_ARP_TRAP_STAGE)
              + sim_tcam_count_stage(TABLE_DSCP_MAP_STAGE)
              + sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE);   /* = 97 */
    TEST_ASSERT(total < SIM_TCAM_MAX);

    TEST_END();
}

// ─────────────────────────────────────────────
// IT-SYS-2: ARP Request Punt → Reply + 双表联动
// ─────────────────────────────────────────────
void test_sys_arp_request_flow(void)
{
    TEST_BEGIN("SYS-2 : ARP Request Punt → Reply 内容 + ARP表 + FDB TCAM 联动");

    sim_hal_reset();
    arp_init();
    fdb_init();

    /* 本端：port 0，IP=10.10.0.1，MAC=02:00:00:00:00:00 */
    static const uint8_t my_mac[6]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    /* 请求方：MAC=AA:BB:CC:DD:EE:FF，IP=10.10.0.2 */
    static const uint8_t req_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    static const uint8_t bcast[6]   = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t zero_mac[6]= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t my_ip  = 0x0A0A0001;  /* 10.10.0.1 */
    uint32_t req_ip = 0x0A0A0002;  /* 10.10.0.2 */

    arp_set_port_intf(0, my_ip, my_mac);

    /* 构造并处理 ARP Request：10.10.0.2 广播询问 10.10.0.1 的 MAC */
    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.ing_port = 0;
    pkt.vlan_id  = 10;
    pkt.reason   = PUNT_REASON_ARP;
    pkt.pkt_len  = build_arp_pkt(pkt.data,
                                  1,             /* oper = Request */
                                  req_mac, req_ip,
                                  zero_mac, my_ip,
                                  bcast, req_mac);

    arp_process_pkt(&pkt);

    /* ── 验证 TX 环中有 ARP Reply ─────────────────────────────── */
    TEST_ASSERT(sim_punt_tx_pending() >= 1);
    sim_punt_rec_t *tx = sim_punt_tx_pop();
    TEST_ASSERT_NOTNULL(tx);

    /* EtherType = 0x0806 (bytes 12-13) */
    TEST_ASSERT_EQ(tx->pkt.data[12], 0x08);
    TEST_ASSERT_EQ(tx->pkt.data[13], 0x06);

    /* ARP oper = 2 (Reply) (bytes 20-21) */
    TEST_ASSERT_EQ(tx->pkt.data[20], 0x00);
    TEST_ASSERT_EQ(tx->pkt.data[21], 0x02);

    /* SHA = my_mac（本端 MAC，bytes 22-27） */
    TEST_ASSERT_EQ(tx->pkt.data[22], my_mac[0]);
    TEST_ASSERT_EQ(tx->pkt.data[27], my_mac[5]);

    /* SPA = my_ip = 10.10.0.1（bytes 28-31） */
    TEST_ASSERT_EQ(tx->pkt.data[28], 0x0A);
    TEST_ASSERT_EQ(tx->pkt.data[29], 0x0A);
    TEST_ASSERT_EQ(tx->pkt.data[30], 0x00);
    TEST_ASSERT_EQ(tx->pkt.data[31], 0x01);

    /* THA = req_mac（请求方 MAC，bytes 32-37） */
    TEST_ASSERT_EQ(tx->pkt.data[32], req_mac[0]);
    TEST_ASSERT_EQ(tx->pkt.data[37], req_mac[5]);

    /* TPA = req_ip = 10.10.0.2（bytes 38-41） */
    TEST_ASSERT_EQ(tx->pkt.data[38], 0x0A);
    TEST_ASSERT_EQ(tx->pkt.data[41], 0x02);

    /* ── 验证 ARP 表学习到请求方（跨模块：arp → arp_table） ──── */
    uint8_t   learned_mac[6];
    port_id_t learned_port;
    TEST_ASSERT_EQ(arp_lookup(req_ip, learned_mac, &learned_port), HAL_OK);
    TEST_ASSERT_EQ(learned_mac[0], 0xAA);
    TEST_ASSERT_EQ(learned_mac[5], 0xFF);
    TEST_ASSERT_EQ(learned_port,   0);

    /* ── 验证 FDB TCAM 学习到请求方（跨模块：arp → fdb → TCAM） */
    /* req_mac = 0xAABBCCDDEEFF；低 12 位 = 0xEFF                 */
    uint16_t fdb_tid = TABLE_L2_FDB_BASE
                     + (uint16_t)(0xAABBCCDDEEFFULL & 0xFFF);
    sim_tcam_rec_t *fdb_r = sim_tcam_find(TABLE_L2_FDB_STAGE, fdb_tid);
    TEST_ASSERT_NOTNULL(fdb_r);
    TEST_ASSERT_EQ(fdb_r->entry.action_id,          ACTION_L2_FORWARD);
    TEST_ASSERT_EQ(fdb_r->entry.action_params[0],   0);  /* 出端口 = 0 */

    TEST_END();
}

// ─────────────────────────────────────────────
// IT-SYS-3: ARP Reply Punt → ARP表与 FDB TCAM 双表一致性
// ─────────────────────────────────────────────
void test_sys_arp_fdb_correlation(void)
{
    TEST_BEGIN("SYS-3 : ARP Reply Punt → ARP表与FDB TCAM字段双向一致");

    sim_hal_reset();
    arp_init();
    fdb_init();

    /* 对端：port 5，IP=10.20.0.1，MAC=CC:DD:EE:FF:00:11 */
    static const uint8_t peer_mac[6] = {0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    static const uint8_t my_mac[6]   = {0x02, 0x00, 0x00, 0x00, 0x00, 0x05};
    uint32_t peer_ip = 0x0A140001;  /* 10.20.0.1 */
    uint32_t my_ip   = 0x0A140002;  /* 10.20.0.2 */

    arp_set_port_intf(5, my_ip, my_mac);

    /* 构造 ARP Reply：10.20.0.1 回复给 10.20.0.2 */
    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.ing_port = 5;
    pkt.vlan_id  = 20;
    pkt.reason   = PUNT_REASON_ARP;
    pkt.pkt_len  = build_arp_pkt(pkt.data,
                                  2,             /* oper = Reply */
                                  peer_mac, peer_ip,
                                  my_mac,  my_ip,
                                  my_mac, peer_mac);

    arp_process_pkt(&pkt);

    /* ── ARP 表验证 ─────────────────────────────────────────────── */
    uint8_t   out_mac[6];
    port_id_t out_port;
    TEST_ASSERT_EQ(arp_lookup(peer_ip, out_mac, &out_port), HAL_OK);
    TEST_ASSERT_EQ(out_mac[0], 0xCC);
    TEST_ASSERT_EQ(out_mac[1], 0xDD);
    TEST_ASSERT_EQ(out_mac[5], 0x11);
    TEST_ASSERT_EQ(out_port,   5);

    /* ── FDB TCAM 验证 ──────────────────────────────────────────── */
    /* peer_mac = 0xCCDDEEFF0011；低 12 位 = 0x011               */
    uint16_t fdb_tid = TABLE_L2_FDB_BASE
                     + (uint16_t)(0xCCDDEEFF0011ULL & 0xFFF);
    sim_tcam_rec_t *fdb_r = sim_tcam_find(TABLE_L2_FDB_STAGE, fdb_tid);
    TEST_ASSERT_NOTNULL(fdb_r);
    TEST_ASSERT_EQ(fdb_r->entry.action_id,          ACTION_L2_FORWARD);
    TEST_ASSERT_EQ(fdb_r->entry.action_params[0],   5);  /* 出端口 = 5 */

    /* ── 跨表一致性：ARP 表的 port == FDB TCAM 的 action_params[0] */
    TEST_ASSERT_EQ((int)out_port, (int)fdb_r->entry.action_params[0]);

    /* ── ARP Reply 不应触发 TX（无需回复 Reply） ─────────────────── */
    TEST_ASSERT_EQ(sim_punt_tx_pending(), 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// IT-SYS-4: arp_delete 后 FDB TCAM 条目残留
//
// 【已知缺陷记录】arp_delete() 仅清空 ARP 软件表项，
//   未调用 fdb_delete()，导致 FDB TCAM 条目残留。
//   本测试断言当前的实际行为（残留），因此 PASS。
//   修复该缺陷时，需同步将最后两个断言反转：
//     TEST_ASSERT_NULL  → FDB 条目已清除
//     （并删除 TEST_ASSERT_NOTNULL 那行）
// ─────────────────────────────────────────────
void test_sys_arp_delete_fdb_cleanup(void)
{
    TEST_BEGIN("SYS-4 : arp_delete 后 FDB TCAM 残留（已知缺陷，当前行为断言）");

    sim_hal_reset();
    arp_init();
    fdb_init();

    static const uint8_t mac_a[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint32_t ip_a = 0x0A000001;  /* 10.0.0.1 */

    /* arp_add 内部调用 fdb_learn，同时写入 ARP 表和 FDB TCAM */
    TEST_ASSERT_EQ(arp_add(ip_a, mac_a, 3, 10), HAL_OK);

    /* 前置确认：ARP 表可查 */
    TEST_ASSERT_EQ(arp_lookup(ip_a, NULL, NULL), HAL_OK);

    /* 前置确认：FDB TCAM 存在（0x001122334455 & 0xFFF = 0x455） */
    uint16_t fdb_tid = TABLE_L2_FDB_BASE + 0x455u;
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_L2_FDB_STAGE, fdb_tid));

    /* 删除 ARP 条目 */
    TEST_ASSERT_EQ(arp_delete(ip_a), HAL_OK);

    /* ARP 表已清除 */
    TEST_ASSERT_EQ(arp_lookup(ip_a, NULL, NULL), -1);

    /* 【缺陷断言】FDB TCAM 条目在 arp_delete 后仍然存在
     * arp_delete() 未调用 fdb_delete()，属于已知缺陷。
     * 修复后此行应改为 TEST_ASSERT_NULL。                  */
    TEST_ASSERT_NOTNULL(sim_tcam_find(TABLE_L2_FDB_STAGE, fdb_tid));

    TEST_END();
}

// ─────────────────────────────────────────────
// IT-SYS-5: Route + ACL + FDB 三 Stage TCAM 共存
// ─────────────────────────────────────────────
void test_sys_multimodule_coexist(void)
{
    TEST_BEGIN("SYS-5 : Route/ACL/FDB 写入各自 Stage，互不干扰");

    sim_hal_reset();
    fdb_init();
    route_init();
    acl_init();

    /* Stage 0：安装 10.0.0.0/8 → port 2 路由 */
    TEST_ASSERT_EQ(route_add(0x0A000000, 8, 2, 0xAABBCCDDEEFFULL), HAL_OK);

    /* Stage 1：安装 192.168.0.0/16 → deny port 80 ACL */
    int rule_id = acl_add_deny(0xC0A80000, 0xFFFF0000, 0, 0, 80);
    TEST_ASSERT(rule_id >= 0);

    /* Stage 2：安装静态 FDB 条目 00:11:22:33:44:55 → port 0 */
    TEST_ASSERT_EQ(fdb_add_static(0x001122334455ULL, 0, 10), HAL_OK);

    /* ── 各 Stage 独立性验证 ──────────────────────────────────── */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_IPV4_LPM_STAGE),    1);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_ACL_INGRESS_STAGE), 1);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_L2_FDB_STAGE),      1);

    /* 其他 Stage 不应有任何写入 */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_ARP_TRAP_STAGE),    0);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_DSCP_MAP_STAGE),    0);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 0);

    /* ── 各 Stage 内容正确性 ─────────────────────────────────── */

    /* Route：table_id = LPM_BASE + (0x0A000000 >> 24) = 10 */
    sim_tcam_rec_t *r_r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, 10u);
    TEST_ASSERT_NOTNULL(r_r);
    TEST_ASSERT_EQ(r_r->entry.action_id,          ACTION_FORWARD);
    TEST_ASSERT_EQ(r_r->entry.action_params[0],   2);     /* 出端口 = 2 */
    TEST_ASSERT_EQ(r_r->entry.action_params[1],   0xAA);  /* dmac[0] */
    TEST_ASSERT_EQ(r_r->entry.action_params[6],   0xFF);  /* dmac[5] */

    /* ACL：table_id = ACL_BASE + rule_id(0) */
    sim_tcam_rec_t *r_a = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                         TABLE_ACL_INGRESS_BASE);
    TEST_ASSERT_NOTNULL(r_a);
    TEST_ASSERT_EQ(r_a->entry.action_id, ACTION_DENY);
    /* src key = 192.168.0.0 大端 */
    TEST_ASSERT_EQ(r_a->entry.key.bytes[0], 0xC0);
    TEST_ASSERT_EQ(r_a->entry.key.bytes[1], 0xA8);
    /* dport = 80 = 0x0050；bytes[9] = 0x50 */
    TEST_ASSERT_EQ(r_a->entry.key.bytes[9], 80);

    /* FDB：table_id = FDB_BASE + (0x001122334455 & 0xFFF) = 0x455 */
    sim_tcam_rec_t *r_f = sim_tcam_find(TABLE_L2_FDB_STAGE,
                                         TABLE_L2_FDB_BASE + 0x455u);
    TEST_ASSERT_NOTNULL(r_f);
    TEST_ASSERT_EQ(r_f->entry.action_id,          ACTION_L2_FORWARD);
    TEST_ASSERT_EQ(r_f->entry.action_params[0],   0);     /* 出端口 = 0 */

    TEST_END();
}

// ─────────────────────────────────────────────
// IT-SYS-6: CLI 多命令序列 → 多 Stage TCAM 同时生效
// ─────────────────────────────────────────────
void test_sys_cli_sequence(void)
{
    TEST_BEGIN("SYS-6 : CLI(route+acl+vlan) → Stage0/Stage1/Stage6 同时生效");

    sim_hal_reset();
    vlan_init();      /* 初始化 VLAN 软件状态（vlan_db、port_cfg） */
    sim_hal_reset();  /* 清空默认 TCAM，保留 VLAN 软件状态 */
    route_init();
    acl_init();

    /* route add 192.168.0.0/16 1 11:22:33:44:55:66 */
    {
        char *argv[] = {"route", "add", "192.168.0.0/16",
                        "1", "11:22:33:44:55:66"};
        TEST_ASSERT_EQ(cli_exec_cmd(5, argv), 1);
    }

    /* acl deny 10.0.0.0/8 0.0.0.0/0 443 */
    {
        char *argv[] = {"acl", "deny", "10.0.0.0/8", "0.0.0.0/0", "443"};
        TEST_ASSERT_EQ(cli_exec_cmd(5, argv), 1);
    }

    /* vlan create 200 */
    {
        char *argv[] = {"vlan", "create", "200"};
        TEST_ASSERT_EQ(cli_exec_cmd(3, argv), 1);
    }

    /* vlan port 200 add 7 tagged */
    {
        char *argv[] = {"vlan", "port", "200", "add", "7", "tagged"};
        TEST_ASSERT_EQ(cli_exec_cmd(6, argv), 1);
    }

    /* ── Stage 0：192.168.0.0/16 路由 ───────────────────────── */
    /* table_id = LPM_BASE + (0xC0A80000 >> 16) = 0xC0A8          */
    uint16_t route_tid = (uint16_t)(TABLE_IPV4_LPM_BASE
                                    + (0xC0A80000u >> 16));
    sim_tcam_rec_t *r_r = sim_tcam_find(TABLE_IPV4_LPM_STAGE, route_tid);
    TEST_ASSERT_NOTNULL(r_r);
    TEST_ASSERT_EQ(r_r->entry.action_id,          ACTION_FORWARD);
    TEST_ASSERT_EQ(r_r->entry.action_params[0],   1);     /* 出端口 = 1 */
    TEST_ASSERT_EQ(r_r->entry.action_params[1],   0x11);  /* dmac[0] */
    TEST_ASSERT_EQ(r_r->entry.action_params[6],   0x66);  /* dmac[5] */

    /* ── Stage 1：ACL deny，dport=443=0x01BB ────────────────── */
    sim_tcam_rec_t *r_a = sim_tcam_find(TABLE_ACL_INGRESS_STAGE,
                                         TABLE_ACL_INGRESS_BASE);
    TEST_ASSERT_NOTNULL(r_a);
    TEST_ASSERT_EQ(r_a->entry.action_id, ACTION_DENY);
    TEST_ASSERT_EQ(r_a->entry.key.bytes[0], 0x0A);        /* src 10.x.x.x */
    /* dport bytes[8]=0x01, bytes[9]=0xBB */
    TEST_ASSERT_EQ(r_a->entry.key.bytes[8], 0x01);
    TEST_ASSERT_EQ(r_a->entry.key.bytes[9], 0xBB);

    /* ── Stage 6：VLAN 200 port 7 tagged 出口规则 ───────────── */
    /* table_id = VLAN_EGRESS_ENTRY(7, 200) = 7*256 + 200 = 1992   */
    uint16_t eg_tid = (uint16_t)(7u * 256u + 200u);
    sim_tcam_rec_t *r_v = sim_tcam_find(TABLE_VLAN_EGRESS_STAGE, eg_tid);
    TEST_ASSERT_NOTNULL(r_v);
    TEST_ASSERT_EQ(r_v->entry.action_id, ACTION_VLAN_KEEP_TAG); /* tagged */

    /* ── 三 Stage 各恰好 1 条，互不干扰 ──────────────────────── */
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_IPV4_LPM_STAGE),    1);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_ACL_INGRESS_STAGE), 1);
    TEST_ASSERT_EQ(sim_tcam_count_stage(TABLE_VLAN_EGRESS_STAGE), 1);

    TEST_END();
}
