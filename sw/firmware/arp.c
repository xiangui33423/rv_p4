// arp.c
// ARP 协议 + 邻居表管理实现

#include "arp.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 软件状态
// ─────────────────────────────────────────────
static arp_entry_t arp_table[ARP_TABLE_SIZE];
static l3_intf_t   l3_intf[32];   // per-port L3 接口

// 广播 MAC
static const uint8_t BCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

static uint16_t arp_hash(uint32_t ip) {
    // FNV-1a 32b → 8b
    uint32_t h = 2166136261UL;
    h ^= (ip & 0xFF);         h *= 16777619UL;
    h ^= ((ip >> 8)  & 0xFF); h *= 16777619UL;
    h ^= ((ip >> 16) & 0xFF); h *= 16777619UL;
    h ^= ((ip >> 24) & 0xFF); h *= 16777619UL;
    return (uint16_t)(h % ARP_TABLE_SIZE);
}

/* 线性探测查找，返回条目指针；若 find_free=1 则可返回空槽 */
static arp_entry_t *arp_find(uint32_t ip, int find_free) {
    uint16_t start = arp_hash(ip);
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        uint16_t idx = (uint16_t)((start + i) % ARP_TABLE_SIZE);
        arp_entry_t *e = &arp_table[idx];
        if (e->state != ARP_STATE_FREE && e->ip == ip)
            return e;
        if (find_free && e->state == ARP_STATE_FREE)
            return e;
    }
    return NULL;
}

static void u32_be(uint8_t *buf, int off, uint32_t v) {
    buf[off]   = (uint8_t)((v >> 24) & 0xFF);
    buf[off+1] = (uint8_t)((v >> 16) & 0xFF);
    buf[off+2] = (uint8_t)((v >>  8) & 0xFF);
    buf[off+3] = (uint8_t)( v        & 0xFF);
}

static uint32_t u32_be_rd(const uint8_t *buf, int off) {
    return ((uint32_t)buf[off]   << 24) |
           ((uint32_t)buf[off+1] << 16) |
           ((uint32_t)buf[off+2] <<  8) |
           ((uint32_t)buf[off+3]      );
}

static uint16_t u16_be_rd(const uint8_t *buf, int off) {
    return (uint16_t)(((uint16_t)buf[off] << 8) | buf[off+1]);
}

/* 安装 ARP Punt TCAM 规则（ingress, stage 3）
 *   key : eth_type(2B)=0x0806  mask: 0xFFFF
 *   action: PUNT_CPU，优先级高（table_id = 0）
 */
static void install_arp_punt_rule(void) {
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    // match key: ethertype 字段（PHV offset 12, 2B）
    e.key.key_len   = 2;
    e.key.bytes[0]  = (ETH_TYPE_ARP >> 8) & 0xFF;
    e.key.bytes[1]  = ETH_TYPE_ARP & 0xFF;
    e.mask.key_len  = 2;
    e.mask.bytes[0] = 0xFF;
    e.mask.bytes[1] = 0xFF;

    e.stage     = TABLE_ARP_TRAP_STAGE;
    e.table_id  = TABLE_ARP_TRAP_BASE;
    e.action_id = ACTION_PUNT_CPU;

    hal_tcam_insert(&e);
}

/* 调用 cp_main.c 中的 fdb_learn（外部链接）*/
extern int fdb_learn(uint64_t dmac, uint8_t port);

/* 构造并发送 ARP 应答 */
static void send_arp_reply(port_id_t eg_port, uint16_t vlan,
                           const uint8_t *req_sha, uint32_t req_spa,
                           const uint8_t *my_mac, uint32_t my_ip) {
    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.eg_port = eg_port;
    pkt.vlan_id = vlan;
    pkt.reason  = PUNT_REASON_ARP;

    uint8_t *p = pkt.data;
    int off = 0;

    /* 以太网头：dst=req sender, src=my_mac, type=0x0806 */
    memcpy(p + off, req_sha, 6);  off += 6;
    memcpy(p + off, my_mac,  6);  off += 6;
    p[off++] = 0x08; p[off++] = 0x06;

    /* ARP 头 */
    p[off++] = 0x00; p[off++] = 0x01;  // htype=Ethernet
    p[off++] = 0x08; p[off++] = 0x00;  // ptype=IPv4
    p[off++] = 6;                        // hlen
    p[off++] = 4;                        // plen
    p[off++] = 0x00; p[off++] = ARP_OP_REPLY;  // oper=Reply
    memcpy(p + off, my_mac,  6);  off += 6;   // sha = my_mac
    u32_be(p, off, my_ip);        off += 4;   // spa = my_ip
    memcpy(p + off, req_sha, 6);  off += 6;   // tha = req sender
    u32_be(p, off, req_spa);      off += 4;   // tpa = req sender IP

    pkt.pkt_len = (uint16_t)off;
    hal_punt_tx_send(&pkt);
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void arp_init(void) {
    memset(arp_table, 0, sizeof(arp_table));
    memset(l3_intf,   0, sizeof(l3_intf));
    install_arp_punt_rule();
}

void arp_set_port_intf(port_id_t port, uint32_t ip, const uint8_t *mac) {
    if (port >= 32 || !mac) return;
    l3_intf[port].ip    = ip;
    l3_intf[port].valid = 1;
    memcpy(l3_intf[port].mac, mac, 6);
}

int arp_add(uint32_t ip, const uint8_t *mac, port_id_t port, uint16_t vlan) {
    if (!mac) return HAL_ERR_INVAL;

    arp_entry_t *e = arp_find(ip, 0);
    if (!e) {
        e = arp_find(ip, 1);   // 找空槽
        if (!e) return HAL_ERR_FULL;
    }

    e->ip        = ip;
    e->port      = port;
    e->vlan      = vlan;
    e->age_ticks = 0;
    e->retry     = ARP_PROBE_RETRY_MAX;
    e->state     = ARP_STATE_REACHABLE;
    memcpy(e->mac, mac, 6);

    // 联动 L2 FDB（将 dmac 学习到对应端口）
    uint64_t dmac = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) |
                    ((uint64_t)mac[2] << 24) | ((uint64_t)mac[3] << 16) |
                    ((uint64_t)mac[4] <<  8) | ((uint64_t)mac[5]);
    fdb_learn(dmac, port);

    return HAL_OK;
}

int arp_delete(uint32_t ip) {
    arp_entry_t *e = arp_find(ip, 0);
    if (!e) return HAL_ERR_INVAL;
    memset(e, 0, sizeof(arp_entry_t));
    return HAL_OK;
}

int arp_lookup(uint32_t ip, uint8_t *mac_out, port_id_t *port_out) {
    arp_entry_t *e = arp_find(ip, 0);
    if (!e || e->state != ARP_STATE_REACHABLE) return -1;
    if (mac_out)  memcpy(mac_out, e->mac, 6);
    if (port_out) *port_out = e->port;
    return HAL_OK;
}

void arp_process_pkt(const punt_pkt_t *pkt) {
    if (!pkt || pkt->pkt_len < 42) return;

    const uint8_t *p = pkt->data;

    /* 解析 ARP 头（Ethernet 头 14B + ARP 28B） */
    uint16_t eth_type = u16_be_rd(p, 12);
    if (eth_type != ETH_TYPE_ARP) return;

    uint16_t oper = u16_be_rd(p, 14 + 6);   // ARP oper
    uint8_t  sha[6];
    memcpy(sha, p + 14 + 8, 6);             // sender hardware addr
    uint32_t spa = u32_be_rd(p, 14 + 14);   // sender protocol addr
    uint32_t tpa = u32_be_rd(p, 14 + 24);   // target protocol addr

    port_id_t ing = pkt->ing_port;

    if (oper == ARP_OP_REQUEST) {
        /* 检查 target IP 是否是本机接口 IP */
        if (ing < 32 && l3_intf[ing].valid && l3_intf[ing].ip == tpa) {
            /* 学习发送方 */
            arp_add(spa, sha, ing, pkt->vlan_id);
            /* 发送 ARP Reply */
            send_arp_reply(ing, pkt->vlan_id,
                           sha, spa,
                           l3_intf[ing].mac, l3_intf[ing].ip);
            printf("ARP Reply: %d.%d.%d.%d → port%d\n",
                   (spa >> 24) & 0xFF, (spa >> 16) & 0xFF,
                   (spa >>  8) & 0xFF,  spa        & 0xFF, ing);
        } else {
            /* 非本机 IP：仍学习发送方（被动学习） */
            arp_add(spa, sha, ing, pkt->vlan_id);
        }

    } else if (oper == ARP_OP_REPLY) {
        /* Reply：更新/添加 ARP 条目 */
        arp_add(spa, sha, ing, pkt->vlan_id);
        printf("ARP learned: %d.%d.%d.%d  port%d  "
               "%02X:%02X:%02X:%02X:%02X:%02X\n",
               (spa >> 24) & 0xFF, (spa >> 16) & 0xFF,
               (spa >>  8) & 0xFF,  spa        & 0xFF, ing,
               sha[0], sha[1], sha[2], sha[3], sha[4], sha[5]);
    }
}

int arp_probe(uint32_t target_ip, port_id_t eg_port, uint16_t vlan) {
    if (eg_port >= 32)            return HAL_ERR_INVAL;
    if (!l3_intf[eg_port].valid)  return HAL_ERR_INVAL;

    /* 标记为 INCOMPLETE 状态 */
    arp_entry_t *e = arp_find(target_ip, 1);
    if (e && e->state == ARP_STATE_FREE) {
        e->ip    = target_ip;
        e->port  = eg_port;
        e->vlan  = vlan;
        e->retry = ARP_PROBE_RETRY_MAX;
        e->state = ARP_STATE_INCOMPLETE;
    }

    punt_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.eg_port = eg_port;
    pkt.vlan_id = vlan;
    pkt.reason  = PUNT_REASON_ARP;

    uint8_t *p = pkt.data;
    int off = 0;

    const uint8_t *my_mac = l3_intf[eg_port].mac;
    uint32_t       my_ip  = l3_intf[eg_port].ip;

    /* 以太网头：dst=broadcast, src=my_mac, type=0x0806 */
    memcpy(p + off, BCAST_MAC, 6); off += 6;
    memcpy(p + off, my_mac,    6); off += 6;
    p[off++] = 0x08; p[off++] = 0x06;

    /* ARP Request */
    p[off++] = 0x00; p[off++] = 0x01;
    p[off++] = 0x08; p[off++] = 0x00;
    p[off++] = 6;
    p[off++] = 4;
    p[off++] = 0x00; p[off++] = ARP_OP_REQUEST;
    memcpy(p + off, my_mac, 6);       off += 6;
    u32_be(p, off, my_ip);            off += 4;
    memcpy(p + off, BCAST_MAC, 6);    off += 6;   // tha = 0
    u32_be(p, off, target_ip);        off += 4;

    pkt.pkt_len = (uint16_t)off;
    return hal_punt_tx_send(&pkt);
}

void arp_age(uint32_t now_sec) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        arp_entry_t *e = &arp_table[i];
        if (e->state == ARP_STATE_FREE) continue;

        uint32_t age = now_sec - e->age_ticks;

        if (e->state == ARP_STATE_INCOMPLETE) {
            if (age >= ARP_INCOMPLETE_TTL) {
                if (e->retry > 0) {
                    e->retry--;
                    e->age_ticks = now_sec;
                    arp_probe(e->ip, e->port, e->vlan);
                } else {
                    printf("ARP incomplete timeout: %d.%d.%d.%d\n",
                           (e->ip >> 24) & 0xFF, (e->ip >> 16) & 0xFF,
                           (e->ip >>  8) & 0xFF,  e->ip        & 0xFF);
                    memset(e, 0, sizeof(arp_entry_t));
                }
            }
        } else if (e->state == ARP_STATE_REACHABLE) {
            if (age >= ARP_AGE_MAX)
                e->state = ARP_STATE_STALE;
        } else if (e->state == ARP_STATE_STALE) {
            /* 发送单播 probe 重新确认 */
            if (age >= ARP_AGE_MAX + ARP_INCOMPLETE_TTL) {
                if (e->retry > 0) {
                    e->retry--;
                    e->age_ticks = now_sec;
                    arp_probe(e->ip, e->port, e->vlan);
                    e->state = ARP_STATE_INCOMPLETE;
                } else {
                    arp_delete(e->ip);
                }
            }
        }
    }
}

void arp_show(void) {
    printf("=== ARP/Neighbor Table ===\n");
    printf("%-18s %-17s %-6s %-6s %s\n",
           "IP", "MAC", "Port", "VLAN", "State");
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        arp_entry_t *e = &arp_table[i];
        if (e->state == ARP_STATE_FREE) continue;
        const char *states[] = {"free", "incomplete", "reachable", "stale"};
        printf("%-3d.%-3d.%-3d.%-3d  "
               "%02X:%02X:%02X:%02X:%02X:%02X  "
               "%-6d %-6d %s\n",
               (e->ip >> 24) & 0xFF, (e->ip >> 16) & 0xFF,
               (e->ip >>  8) & 0xFF,  e->ip        & 0xFF,
               e->mac[0], e->mac[1], e->mac[2],
               e->mac[3], e->mac[4], e->mac[5],
               e->port, e->vlan,
               states[e->state]);
    }
}
