// test_arp.c
// ARP/邻居表模块测试用例（7 个）
//
// 用例列表：
//   1. test_arp_punt_rule          — arp_init() 安装 ARP Punt TCAM 规则
//   2. test_arp_add_lookup_hit     — 添加条目后 lookup 命中
//   3. test_arp_add_lookup_miss    — 查询不存在 IP 返回 -1
//   4. test_arp_delete             — 删除后 lookup 失败
//   5. test_arp_process_request    — 注入 ARP Request → 验证 Reply 格式
//   6. test_arp_process_reply      — 注入 ARP Reply → 邻居表学习 + FDB 联动
//   7. test_arp_age_cycle          — REACHABLE → STALE → INCOMPLETE + probe

#include <string.h>
#include "test_framework.h"
#include "sim_hal.h"
#include "arp.h"
#include "table_map.h"

// ─────────────────────────────────────────────
// 构造 ARP 包工具函数（测试专用）
// ─────────────────────────────────────────────

/*
 * build_arp_pkt — 向 buf 写入完整以太网 + ARP 头
 *   oper: ARP_OP_REQUEST(1) 或 ARP_OP_REPLY(2)
 *   tha:  目标 MAC（Request 时传 NULL → 全零）
 */
static void build_arp_pkt(uint8_t *buf, uint16_t *out_len,
                           const uint8_t *sha, uint32_t spa,
                           const uint8_t *tha, uint32_t tpa,
                           uint16_t oper) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t zero6[6] = {0};
    int off = 0;

    /* 以太网头 */
    memcpy(buf + off,
           (oper == ARP_OP_REQUEST) ? bcast : (tha ? tha : zero6),
           6);            off += 6;
    memcpy(buf + off, sha, 6); off += 6;
    buf[off++] = 0x08; buf[off++] = 0x06;   /* EtherType = ARP */

    /* ARP 头 */
    buf[off++] = 0x00; buf[off++] = 0x01;   /* htype = Ethernet */
    buf[off++] = 0x08; buf[off++] = 0x00;   /* ptype = IPv4 */
    buf[off++] = 6;                           /* hlen */
    buf[off++] = 4;                           /* plen */
    buf[off++] = (uint8_t)((oper >> 8) & 0xFF);
    buf[off++] = (uint8_t)( oper       & 0xFF);

    /* sha / spa */
    memcpy(buf + off, sha, 6); off += 6;
    buf[off++] = (uint8_t)((spa >> 24) & 0xFF);
    buf[off++] = (uint8_t)((spa >> 16) & 0xFF);
    buf[off++] = (uint8_t)((spa >>  8) & 0xFF);
    buf[off++] = (uint8_t)( spa        & 0xFF);

    /* tha / tpa */
    memcpy(buf + off, tha ? tha : zero6, 6); off += 6;
    buf[off++] = (uint8_t)((tpa >> 24) & 0xFF);
    buf[off++] = (uint8_t)((tpa >> 16) & 0xFF);
    buf[off++] = (uint8_t)((tpa >>  8) & 0xFF);
    buf[off++] = (uint8_t)( tpa        & 0xFF);

    *out_len = (uint16_t)off;   /* = 42 */
}

/* 从包数据中读取大端 uint32 */
static uint32_t pkt_u32(const uint8_t *d, int off) {
    return ((uint32_t)d[off]<<24)|((uint32_t)d[off+1]<<16)|
           ((uint32_t)d[off+2]<<8)|d[off+3];
}

/* 从包数据中读取大端 uint16 */
static uint16_t pkt_u16(const uint8_t *d, int off) {
    return (uint16_t)(((uint16_t)d[off] << 8) | d[off+1]);
}

// ─────────────────────────────────────────────
// TC-ARP-1: ARP Punt 规则安装
// ─────────────────────────────────────────────
void test_arp_punt_rule(void) {
    TEST_BEGIN("ARP-1 : arp_init() installs ARP punt TCAM rule");

    sim_hal_reset();
    arp_init();

    sim_tcam_rec_t *r = sim_tcam_find(TABLE_ARP_TRAP_STAGE, TABLE_ARP_TRAP_BASE);
    TEST_ASSERT_NOTNULL(r);
    TEST_ASSERT_EQ(r->entry.action_id, ACTION_PUNT_CPU);

    /* key = EtherType 0x0806 (大端 2B) */
    TEST_ASSERT_EQ(r->entry.key.bytes[0], 0x08);
    TEST_ASSERT_EQ(r->entry.key.bytes[1], 0x06);
    TEST_ASSERT_EQ(r->entry.key.key_len,  2);

    /* mask 全 1（精确匹配） */
    TEST_ASSERT_EQ(r->entry.mask.bytes[0], 0xFF);
    TEST_ASSERT_EQ(r->entry.mask.bytes[1], 0xFF);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-2: 添加条目后 lookup 命中
// ─────────────────────────────────────────────
void test_arp_add_lookup_hit(void) {
    TEST_BEGIN("ARP-2 : arp_add + arp_lookup hit");

    sim_hal_reset();
    arp_init();

    const uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_OK(arp_add(0x0A000001, mac, 2, 10));

    uint8_t   out_mac[6] = {0};
    port_id_t out_port   = 0xFF;
    TEST_ASSERT_OK(arp_lookup(0x0A000001, out_mac, &out_port));
    TEST_ASSERT_EQ(out_port, 2);
    TEST_ASSERT_MEM_EQ(out_mac, mac, 6);

    /* arp_add 联动 fdb_learn → Stage 2 TCAM 中有该 MAC 的转发条目 */
    /* mac = {0x00,0x11,0x22,0x33,0x44,0x55}，dmac & 0xFFF = 0x455 */
    sim_tcam_rec_t *fdb_r = sim_tcam_find(TABLE_L2_FDB_STAGE,
                                           TABLE_L2_FDB_BASE + 0x455u);
    TEST_ASSERT_NOTNULL(fdb_r);
    TEST_ASSERT_EQ(fdb_r->entry.action_params[0], 2);   /* port=2 */

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-3: lookup miss
// ─────────────────────────────────────────────
void test_arp_add_lookup_miss(void) {
    TEST_BEGIN("ARP-3 : arp_lookup miss for unknown IP");

    sim_hal_reset();
    arp_init();

    uint8_t   mac[6];
    port_id_t p;
    TEST_ASSERT_EQ(arp_lookup(0x0B000001, mac, &p), -1);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-4: 删除后 lookup 失败
// ─────────────────────────────────────────────
void test_arp_delete(void) {
    TEST_BEGIN("ARP-4 : arp_delete removes entry");

    sim_hal_reset();
    arp_init();

    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    arp_add(0x0A000002, mac, 1, 1);
    TEST_ASSERT_OK(arp_delete(0x0A000002));

    uint8_t   om[6];
    port_id_t op;
    TEST_ASSERT_EQ(arp_lookup(0x0A000002, om, &op), -1);

    /* 删除不存在的 IP 返回错误 */
    TEST_ASSERT_NE(arp_delete(0x0A000002), HAL_OK);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-5: 处理 ARP Request → 验证 Reply
// ─────────────────────────────────────────────
void test_arp_process_request(void) {
    TEST_BEGIN("ARP-5 : ARP Request for local IP generates Reply");

    sim_hal_reset();
    arp_init();

    const uint8_t my_mac[6]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint32_t my_ip     = 0x0A0A0001;  /* 10.10.0.1 */
    arp_set_port_intf(0, my_ip, my_mac);

    const uint8_t sender_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint32_t sender_ip    = 0x0A0A0002;  /* 10.10.0.2 */

    /* 构造 ARP Request */
    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.ing_port = 0;
    pkt.vlan_id  = 10;
    pkt.reason   = PUNT_REASON_ARP;
    build_arp_pkt(pkt.data, &pkt.pkt_len,
                  sender_mac, sender_ip,
                  NULL,       my_ip,
                  ARP_OP_REQUEST);

    arp_process_pkt(&pkt);

    /* 应发出 1 个 Reply */
    TEST_ASSERT_EQ(sim_punt_tx_pending(), 1);
    sim_punt_rec_t *tx = sim_punt_tx_pop();
    TEST_ASSERT_NOTNULL(tx);

    const uint8_t *d = tx->pkt.data;

    /* ETH: dst = sender_mac */
    TEST_ASSERT_MEM_EQ(d, sender_mac, 6);
    /* ETH: src = my_mac */
    TEST_ASSERT_MEM_EQ(d + 6, my_mac, 6);
    /* EtherType = 0x0806 */
    TEST_ASSERT_EQ(pkt_u16(d, 12), 0x0806);
    /* ARP oper = 2 (Reply) */
    TEST_ASSERT_EQ(pkt_u16(d, 20), ARP_OP_REPLY);
    /* ARP sha = my_mac */
    TEST_ASSERT_MEM_EQ(d + 22, my_mac, 6);
    /* ARP spa = my_ip */
    TEST_ASSERT_EQ(pkt_u32(d, 28), my_ip);
    /* ARP tha = sender_mac */
    TEST_ASSERT_MEM_EQ(d + 32, sender_mac, 6);
    /* ARP tpa = sender_ip */
    TEST_ASSERT_EQ(pkt_u32(d, 38), sender_ip);

    /* 同时学习了 sender 的邻居条目 */
    uint8_t om[6]; port_id_t op;
    TEST_ASSERT_OK(arp_lookup(sender_ip, om, &op));
    TEST_ASSERT_MEM_EQ(om, sender_mac, 6);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-6: 处理 ARP Reply → 学习邻居 + FDB
// ─────────────────────────────────────────────
void test_arp_process_reply(void) {
    TEST_BEGIN("ARP-6 : ARP Reply learns neighbor entry and calls fdb_learn");

    sim_hal_reset();
    arp_init();

    const uint8_t reply_mac[6] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    const uint32_t reply_ip    = 0xC0A80102;  /* 192.168.1.2 */

    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.ing_port = 5;
    pkt.vlan_id  = 20;
    pkt.reason   = PUNT_REASON_ARP;
    build_arp_pkt(pkt.data, &pkt.pkt_len,
                  reply_mac, reply_ip,
                  NULL, 0x00000000,
                  ARP_OP_REPLY);

    arp_process_pkt(&pkt);

    /* 邻居表中应有该条目 */
    uint8_t om[6]; port_id_t op;
    TEST_ASSERT_OK(arp_lookup(reply_ip, om, &op));
    TEST_ASSERT_MEM_EQ(om, reply_mac, 6);
    TEST_ASSERT_EQ(op, 5);

    /* fdb_learn 被调用 → Stage 2 TCAM 中有该 MAC 的转发条目 */
    /* reply_mac = {0x00,0xAA,0xBB,0xCC,0xDD,0xEE}, dmac & 0xFFF = 0xDEE */
    sim_tcam_rec_t *fdb_r2 = sim_tcam_find(TABLE_L2_FDB_STAGE,
                                            TABLE_L2_FDB_BASE + 0xDEEu);
    TEST_ASSERT_NOTNULL(fdb_r2);
    TEST_ASSERT_EQ(fdb_r2->entry.action_params[0], 5);   /* port=5 */

    /* 不产生 TX 包（Reply 不回复） */
    TEST_ASSERT_EQ(sim_punt_tx_pending(), 0);

    TEST_END();
}

// ─────────────────────────────────────────────
// TC-ARP-7: 老化周期 REACHABLE → STALE → probe
// ─────────────────────────────────────────────
void test_arp_age_cycle(void) {
    TEST_BEGIN("ARP-7 : aging: REACHABLE → STALE → INCOMPLETE + probe");

    sim_hal_reset();
    arp_init();

    /* 配置 L3 接口（probe 发送需要） */
    const uint8_t my_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    arp_set_port_intf(0, 0x0A0A0001, my_mac);

    const uint8_t mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    arp_add(0x0A0A0064, mac, 0, 1);

    /* --- 1. 未老化：lookup 应命中 --- */
    uint8_t om[6]; port_id_t op;
    TEST_ASSERT_OK(arp_lookup(0x0A0A0064, om, &op));

    /* --- 2. 老化到 ARP_AGE_MAX+1：变为 STALE --- */
    arp_age(ARP_AGE_MAX + 1);
    TEST_ASSERT_EQ(arp_lookup(0x0A0A0064, om, &op), -1);  /* STALE → lookup -1 */

    /* --- 3. 继续老化触发 probe --- */
    arp_age(ARP_AGE_MAX + ARP_INCOMPLETE_TTL + 1);
    /* 应产生 ARP Request（probe） */
    TEST_ASSERT(sim_punt_tx_pending() > 0);

    sim_punt_rec_t *tx = sim_punt_tx_pop();
    TEST_ASSERT_NOTNULL(tx);
    const uint8_t *d = tx->pkt.data;

    /* probe 是 ARP Request (oper=1) */
    TEST_ASSERT_EQ(pkt_u16(d, 20), ARP_OP_REQUEST);
    /* spa = my_ip */
    TEST_ASSERT_EQ(pkt_u32(d, 28), 0x0A0A0001);
    /* tpa = neighbor IP */
    TEST_ASSERT_EQ(pkt_u32(d, 38), 0x0A0A0064);

    TEST_END();
}
