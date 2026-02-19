// test_dp_cosim.c
// 数据面 + 控制面联合测试（Co-Simulation，7 个场景）
//
// 测试思路：
//   通过控制面 API（route_add/acl_add_deny/fdb_add_static/arp_init/qos_init/vlan_*）
//   向 sim_hal TCAM 数据库写入规则，再用 pkt_process() 将原始以太帧送入
//   PISA 功能模型（pkt_model.c），验证最终转发决策是否与预期一致。
//
// 场景列表：
//   CS-1: IPv4 LPM 路由命中 → 出端口正确，dst MAC 被改写
//   CS-2: ACL Deny → 报文被丢弃（与路由规则共存）
//   CS-3: L2 FDB 精确转发 → 出端口正确
//   CS-4: ARP Punt → 上送 CPU
//   CS-5: DSCP QoS 优先级映射 → qos_prio 正确
//   CS-6: VLAN 入口 PVID 分配 → vlan_id 正确赋值
//   CS-7: 全流水线 (路由 + VLAN 入口 + VLAN 出口) → 端口 + 标签剥离

#include <string.h>
#include <stdio.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "pkt_model.h"
#include "table_map.h"

// 固件模块
#include "route.h"
#include "acl.h"
#include "fdb.h"
#include "arp.h"
#include "qos.h"
#include "vlan.h"

// ─────────────────────────────────────────────
// 内部工具：报文构造
// ─────────────────────────────────────────────

// 构造最小以太帧（含 IPv4 头，无 L4 payload）
// 返回总字节数（固定 34 字节）
static uint16_t build_ipv4_pkt(uint8_t *buf,
                                const uint8_t *eth_dst,
                                const uint8_t *eth_src,
                                uint8_t tos,       // DSCP + ECN 字节
                                uint32_t src_ip,
                                uint32_t dst_ip,
                                uint8_t  proto,    // TCP=6, UDP=17, ICMP=1
                                uint16_t dport)    // 0 = 不添加 L4
{
    int off = 0;

    // 以太网头（14 B）
    memcpy(buf + off, eth_dst, 6); off += 6;
    memcpy(buf + off, eth_src, 6); off += 6;
    buf[off++] = 0x08;
    buf[off++] = 0x00;   // EtherType = IPv4

    // IPv4 基础头（20 B）
    buf[off++] = 0x45;                          // Version=4, IHL=5
    buf[off++] = tos;                           // TOS（DSCP + ECN）
    buf[off++] = 0x00;
    buf[off++] = (dport ? 24 : 20);             // Total Length
    buf[off++] = 0x00; buf[off++] = 0x01;      // ID
    buf[off++] = 0x00; buf[off++] = 0x00;      // Flags + Frag offset
    buf[off++] = 64;                            // TTL
    buf[off++] = proto;                         // Protocol
    buf[off++] = 0x00; buf[off++] = 0x00;      // Checksum（测试不校验）
    buf[off++] = (uint8_t)((src_ip >> 24) & 0xFF);
    buf[off++] = (uint8_t)((src_ip >> 16) & 0xFF);
    buf[off++] = (uint8_t)((src_ip >>  8) & 0xFF);
    buf[off++] = (uint8_t)( src_ip        & 0xFF);
    buf[off++] = (uint8_t)((dst_ip >> 24) & 0xFF);
    buf[off++] = (uint8_t)((dst_ip >> 16) & 0xFF);
    buf[off++] = (uint8_t)((dst_ip >>  8) & 0xFF);
    buf[off++] = (uint8_t)( dst_ip        & 0xFF);

    // L4 头（如有 dport，添加 4 字节 TCP/UDP 首部）
    if (dport) {
        buf[off++] = 0x00; buf[off++] = 0x50;  // sport = 80（任意）
        buf[off++] = (uint8_t)((dport >> 8) & 0xFF);
        buf[off++] = (uint8_t)( dport       & 0xFF);
    }

    return (uint16_t)off;
}

// 构造纯 L2 以太帧（非 IPv4，用于 FDB 测试）
static uint16_t build_l2_pkt(uint8_t *buf,
                               const uint8_t *eth_dst,
                               const uint8_t *eth_src,
                               uint16_t ethertype)
{
    memcpy(buf,     eth_dst, 6);
    memcpy(buf + 6, eth_src, 6);
    buf[12] = (uint8_t)((ethertype >> 8) & 0xFF);
    buf[13] = (uint8_t)( ethertype       & 0xFF);
    // 4 字节 payload（全零，防止过短）
    buf[14] = buf[15] = buf[16] = buf[17] = 0;
    return 18;
}

// 构造 ARP 请求帧（42 字节）
static uint16_t build_arp_pkt(uint8_t *buf,
                                const uint8_t *sha, uint32_t spa,
                                uint32_t tpa)
{
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t zero [6] = {0x00,0x00,0x00,0x00,0x00,0x00};
    int off = 0;

    memcpy(buf + off, bcast, 6); off += 6;   // dst = broadcast
    memcpy(buf + off, sha,   6); off += 6;   // src = sender MAC
    buf[off++] = 0x08; buf[off++] = 0x06;   // EtherType = ARP

    buf[off++] = 0x00; buf[off++] = 0x01;   // htype = Ethernet
    buf[off++] = 0x08; buf[off++] = 0x00;   // ptype = IPv4
    buf[off++] = 6;    buf[off++] = 4;      // hlen, plen
    buf[off++] = 0x00; buf[off++] = 0x01;   // oper = Request

    memcpy(buf + off, sha, 6); off += 6;                                  // SHA
    buf[off++]=(spa>>24)&0xFF; buf[off++]=(spa>>16)&0xFF;
    buf[off++]=(spa>> 8)&0xFF; buf[off++]=(spa    )&0xFF; // SPA
    memcpy(buf + off, zero, 6); off += 6;                                 // THA
    buf[off++]=(tpa>>24)&0xFF; buf[off++]=(tpa>>16)&0xFF;
    buf[off++]=(tpa>> 8)&0xFF; buf[off++]=(tpa    )&0xFF; // TPA

    return (uint16_t)off;   /* = 42 */
}

// ─────────────────────────────────────────────
// CS-1: IPv4 LPM 路由命中 → 正确出端口 + dst MAC 改写
// ─────────────────────────────────────────────
void test_dp_cosim_route_forward(void)
{
    TEST_BEGIN("CS-1 : IPv4 LPM 路由 → 出端口 3，dst MAC 改写");

    sim_hal_reset();
    route_init();

    // 安装路由：10.10.0.0/16 → port 3，next-hop MAC = AA:BB:CC:DD:EE:FF
    uint64_t nhop_mac = 0xAABBCCDDEEFFULL;
    TEST_ASSERT_OK(route_add(0x0A0A0000u, 16, 3, nhop_mac));

    // 构造报文：IPv4 dst = 10.10.5.99，从 port 0 进入
    static const uint8_t eth_dst[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t eth_src[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t  pkt[64];
    uint16_t len = build_ipv4_pkt(pkt, eth_dst, eth_src, 0x00,
                                   0x01020304u,   // src = 1.2.3.4
                                   0x0A0A0563u,   // dst = 10.10.5.99
                                   0, 0);

    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_process(pkt, len, /*ing_port=*/0, &res), 0);

    // 验证转发决策
    TEST_ASSERT_EQ(res.drop,    0);
    TEST_ASSERT_EQ(res.punt,    0);
    TEST_ASSERT_EQ(res.eg_port, 3);

    // 验证 dst MAC 改写（通过 pkt_forward 修改 PHV，用 phv 直接检验）
    phv_t phv;
    TEST_ASSERT_EQ(pkt_parse(pkt, len, 0, &phv), 0);
    pkt_forward(&phv, &res);
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 0], 0xAA);
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 1], 0xBB);
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 5], 0xFF);

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-2: ACL Deny → 报文丢弃（与路由共存）
// ─────────────────────────────────────────────
void test_dp_cosim_acl_deny(void)
{
    TEST_BEGIN("CS-2 : ACL Deny → 已路由报文在 Stage1 被丢弃");

    sim_hal_reset();
    route_init();
    acl_init();

    // 安装路由：192.168.0.0/16 → port 1
    TEST_ASSERT_OK(route_add(0xC0A80000u, 16, 1, 0x001122334455ULL));

    // 安装 ACL：拒绝来自 172.16.0.0/12 的所有流量（dport=0 = 通配）
    int rid = acl_add_deny(0xAC100000u, 0xFFF00000u, 0, 0, 0);
    TEST_ASSERT(rid >= 0);

    // 构造匹配 ACL 的报文：src=172.16.1.2，dst=192.168.0.1
    static const uint8_t d[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t s[6] = {0x00,0xAA,0xBB,0xCC,0xDD,0xEE};
    uint8_t  pkt[64];
    uint16_t len = build_ipv4_pkt(pkt, d, s, 0x00,
                                   0xAC100102u,   // src = 172.16.1.2
                                   0xC0A80001u,   // dst = 192.168.0.1
                                   6, 80);

    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_process(pkt, len, 0, &res), 0);

    // Stage 0 路由命中（eg_port=1），但 Stage 1 ACL Deny 强制 drop
    TEST_ASSERT_EQ(res.drop, 1);
    TEST_ASSERT_EQ(res.punt, 0);

    // ── 对照：不匹配 ACL 的报文不被丢弃 ──────────────────────────
    uint8_t  pkt2[64];
    uint16_t len2 = build_ipv4_pkt(pkt2, d, s, 0x00,
                                    0x0A000001u,   // src = 10.0.0.1（不在 172.16/12）
                                    0xC0A80002u,   // dst = 192.168.0.2
                                    6, 80);
    fwd_result_t res2;
    TEST_ASSERT_EQ(pkt_process(pkt2, len2, 0, &res2), 0);
    TEST_ASSERT_EQ(res2.drop,    0);
    TEST_ASSERT_EQ(res2.eg_port, 1);

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-3: L2 FDB 精确转发
// ─────────────────────────────────────────────
void test_dp_cosim_fdb_forward(void)
{
    TEST_BEGIN("CS-3 : L2 FDB 精确转发 → 出端口 7");

    sim_hal_reset();
    fdb_init();

    // 安装静态 FDB 条目：MAC=DE:AD:BE:EF:00:01 → port 7
    TEST_ASSERT_OK(fdb_add_static(0xDEADBEEF0001ULL, 7, 0));

    // 构造 L2 帧（非 IPv4，EtherType=0x9999）
    static const uint8_t known_mac[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};
    static const uint8_t src_mac [6]  = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    uint8_t  pkt[18];
    uint16_t len = build_l2_pkt(pkt, known_mac, src_mac, 0x9999);

    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_process(pkt, len, 0, &res), 0);

    TEST_ASSERT_EQ(res.drop,    0);
    TEST_ASSERT_EQ(res.punt,    0);
    TEST_ASSERT_EQ(res.eg_port, 7);

    // ── 对照：未知 dst MAC → Stage 2 未命中，不改变默认端口 ────────
    static const uint8_t unknown_mac[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    uint8_t  pkt2[18];
    uint16_t len2 = build_l2_pkt(pkt2, unknown_mac, src_mac, 0x9999);
    fwd_result_t res2;
    TEST_ASSERT_EQ(pkt_process(pkt2, len2, 0, &res2), 0);
    TEST_ASSERT_EQ(res2.drop,    0);
    TEST_ASSERT_NE(res2.eg_port, 7);   // 不应转发到 port 7

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-4: ARP Punt → 上送 CPU
// ─────────────────────────────────────────────
void test_dp_cosim_arp_punt(void)
{
    TEST_BEGIN("CS-4 : ARP 报文命中 Stage3 Punt 规则 → 上送 CPU");

    sim_hal_reset();
    arp_init();   // 安装 ARP Punt TCAM 规则（Stage 3，ethertype=0x0806）
    fdb_init();

    static const uint8_t my_mac[6] = {0x02,0x00,0x00,0x00,0x00,0x00};
    uint8_t  pkt[42];
    uint16_t len = build_arp_pkt(pkt, my_mac, 0x0A000001u, 0x0A000002u);

    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_process(pkt, len, 0, &res), 0);

    TEST_ASSERT_EQ(res.punt, 1);   // 必须上送 CPU
    TEST_ASSERT_EQ(res.drop, 0);   // 不丢弃

    // ── 对照：普通 IPv4 报文不应被 Punt ─────────────────────────────
    static const uint8_t d[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t s[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t  pkt2[34];
    uint16_t len2 = build_ipv4_pkt(pkt2, d, s, 0, 0x01020304u, 0x0A000001u, 0, 0);
    fwd_result_t res2;
    TEST_ASSERT_EQ(pkt_process(pkt2, len2, 0, &res2), 0);
    TEST_ASSERT_EQ(res2.punt, 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-5: DSCP QoS 优先级映射
// ─────────────────────────────────────────────
void test_dp_cosim_dscp_qos(void)
{
    TEST_BEGIN("CS-5 : DSCP=46(EF) → qos_prio=5；DSCP=0(BE) → qos_prio=0");

    sim_hal_reset();
    qos_init();   // RFC 4594 默认映射：EF(46)→Q5，CS7(56)→Q7 等

    static const uint8_t d[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t s[6] = {0x00,0x11,0x22,0x33,0x44,0x55};

    // 报文 A：DSCP=46（TOS = 46<<2 = 0xB8），期望 qos_prio=5（EF）
    uint8_t  pktA[34];
    uint16_t lenA = build_ipv4_pkt(pktA, d, s,
                                    (uint8_t)(46u << 2),   // TOS = 0xB8
                                    0x01020304u, 0x0A000001u, 0, 0);
    fwd_result_t resA;
    TEST_ASSERT_EQ(pkt_process(pktA, lenA, 0, &resA), 0);
    TEST_ASSERT_EQ(resA.qos_prio, 5);
    TEST_ASSERT_EQ(resA.drop,     0);

    // 报文 B：DSCP=56（TOS = 56<<2 = 0xE0），期望 qos_prio=7（CS7）
    uint8_t  pktB[34];
    uint16_t lenB = build_ipv4_pkt(pktB, d, s,
                                    (uint8_t)(56u << 2),   // TOS = 0xE0
                                    0x01020304u, 0x0A000001u, 0, 0);
    fwd_result_t resB;
    TEST_ASSERT_EQ(pkt_process(pktB, lenB, 0, &resB), 0);
    TEST_ASSERT_EQ(resB.qos_prio, 7);

    // 报文 C：DSCP=0（Best Effort），期望 qos_prio=0
    uint8_t  pktC[34];
    uint16_t lenC = build_ipv4_pkt(pktC, d, s, 0x00,
                                    0x01020304u, 0x0A000001u, 0, 0);
    fwd_result_t resC;
    TEST_ASSERT_EQ(pkt_process(pktC, lenC, 0, &resC), 0);
    TEST_ASSERT_EQ(resC.qos_prio, 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-6: VLAN 入口 PVID 分配
// ─────────────────────────────────────────────
void test_dp_cosim_vlan_ingress(void)
{
    TEST_BEGIN("CS-6 : VLAN 入口 PVID — port0 无标签帧 → vlan_id=10");

    sim_hal_reset();
    vlan_init();
    // vlan_init() 只安装出口规则（Stage 6）；入口规则由 vlan_install_port_rules()
    // 在 vlan_port_set_pvid / vlan_port_set_mode 中按需安装。
    // 此处为 port 1 显式安装入口规则（PVID=1，access 模式）。
    vlan_install_port_rules(1);   // port 1 → PVID=1（默认值不变，触发规则写入）

    // 创建 VLAN 10，将 port 0 加入（无标签出），并将 PVID 改为 10
    TEST_ASSERT_OK(vlan_create(10));
    TEST_ASSERT_OK(vlan_port_add(10, 0, /*tagged=*/0));
    TEST_ASSERT_OK(vlan_port_set_pvid(0, 10));   // 更新 port 0 入口规则：PVID=10

    // 无标签以太帧，从 port 0 进入
    static const uint8_t d[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t s[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t  pkt[18];
    uint16_t len = build_l2_pkt(pkt, d, s, 0x9999);

    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_process(pkt, len, /*ing_port=*/0, &res), 0);

    TEST_ASSERT_EQ(res.drop,    0);
    // Stage 4 命中 port 0 入口规则：ASSIGN_PVID → vlan_id = 10
    TEST_ASSERT_EQ(res.vlan_id, 10);

    // ── 对照：port 1 的 PVID 仍为 1 ─────────────────────────────────
    fwd_result_t res2;
    TEST_ASSERT_EQ(pkt_process(pkt, len, /*ing_port=*/1, &res2), 0);
    TEST_ASSERT_EQ(res2.vlan_id, 1);   // port 1 的 PVID=1 入口规则已安装

    TEST_END();
}

// ─────────────────────────────────────────────
// CS-7: 全流水线 — 路由 + VLAN 入口 + VLAN 出口标签剥离
// ─────────────────────────────────────────────
void test_dp_cosim_full_pipeline(void)
{
    TEST_BEGIN("CS-7 : 全流水线 路由(S0)+VLAN入口(S4)+VLAN出口剥离(S6)");

    sim_hal_reset();
    // vlan_init() 安装出口规则（Stage 6，所有端口 VLAN 1 STRIP）；
    // 同时初始化软件状态（port_cfg[*].pvid=1, mode=ACCESS）。
    vlan_init();
    // 为 port 0 显式安装入口规则（PVID=1，access），使 Stage 4 生效。
    vlan_install_port_rules(0);

    route_init();
    acl_init();
    fdb_init();

    // 安装路由：10.0.0.0/8 → port 4，next-hop MAC = DE:AD:BE:EF:00:FF
    TEST_ASSERT_OK(route_add(0x0A000000u, 8, 4, 0xDEADBEEF00FFULL));

    // 构造无标签 IPv4 报文：src=1.2.3.4，dst=10.1.2.3，从 port 0 进入
    static const uint8_t d[6] = {0x00,0xDE,0xAD,0xBE,0xEF,0xFF};
    static const uint8_t s[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    uint8_t  pkt[34];
    uint16_t len = build_ipv4_pkt(pkt, d, s, 0x00,
                                   0x01020304u,   // src = 1.2.3.4
                                   0x0A010203u,   // dst = 10.1.2.3（匹配 10.0.0.0/8）
                                   0, 0);

    phv_t        phv;
    fwd_result_t res;
    TEST_ASSERT_EQ(pkt_parse(pkt, len, 0, &phv), 0);
    TEST_ASSERT_EQ(pkt_forward(&phv, &res), 0);

    // Stage 0: 路由命中 → eg_port = 4
    TEST_ASSERT_EQ(res.eg_port, 4);
    TEST_ASSERT_EQ(res.drop,    0);
    TEST_ASSERT_EQ(res.punt,    0);

    // Stage 4: VLAN 入口，port 0 PVID=1，无标签帧 → vlan_id = 1
    TEST_ASSERT_EQ(res.vlan_id, 1);

    // Stage 6: VLAN 出口，(eg_port=4, vlan_id=1) → STRIP_TAG（port 4 在 VLAN 1 access）
    TEST_ASSERT_EQ(res.vlan_action, VLAN_ACT_STRIP);

    // dst MAC 改写验证
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 0], 0xDE);
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 1], 0xAD);
    TEST_ASSERT_EQ(phv.hdr[PHV_OFF_ETH_DST + 5], 0xFF);

    TEST_END();
}
