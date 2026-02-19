// pkt_model.c
// 数据面功能模型实现
//
// PISA 流水线仿真：7 个 MAU Stage（0-6），使用 sim_hal.c 的 TCAM 数据库。
// 三值匹配规则：(pkt_key[i] & mask[i]) == (entry_key[i] & mask[i])

#include "pkt_model.h"
#include "sim_hal.h"
#include <string.h>

// ─────────────────────────────────────────────
// 内部：三值 TCAM 查找
// ─────────────────────────────────────────────
// 在 sim_tcam_db 中对指定 stage 做三值匹配（first-match，与 sim_hal 扫描顺序一致）。
// 匹配条件：对每个字节 i（i < cmp_len）：
//   (pkt_key[i] & entry.mask[i]) == (entry.key[i] & entry.mask[i])
// 返回第一个命中的条目指针；无命中返回 NULL。

static sim_tcam_rec_t *tcam_ternary_lookup(uint8_t stage,
                                            const uint8_t *key,
                                            uint8_t key_len)
{
    for (int i = 0; i < sim_tcam_n; i++) {
        sim_tcam_rec_t *r = &sim_tcam_db[i];
        if (!r->valid || r->deleted)          continue;
        if (r->entry.stage != stage)          continue;

        uint8_t match    = 1;
        uint8_t cmp_len  = r->entry.key.key_len;
        if (cmp_len > key_len) cmp_len = key_len;

        for (uint8_t b = 0; b < cmp_len; b++) {
            if ((key[b] & r->entry.mask.bytes[b]) !=
                (r->entry.key.bytes[b] & r->entry.mask.bytes[b])) {
                match = 0;
                break;
            }
        }
        if (match) return r;
    }
    return NULL;
}

// ─────────────────────────────────────────────
// 内部：每级 PHV 关键字提取函数
// ─────────────────────────────────────────────
// 各 Stage 按固件约定从 PHV 中提取匹配键；与各 firmware 模块中 key 编码严格对应。

// Stage 0 — IPv4 LPM：匹配 ipv4_dst（4 字节，PHV offset 34）
static void extract_s0(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    memcpy(key, &phv->hdr[PHV_OFF_IPV4_DST], 4);
    *klen = 4;
}

// Stage 1 — ACL：匹配 ipv4_src(4B) + ipv4_dst(4B) + tcp/udp_dport(2B)
static void extract_s1(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    memcpy(key,   &phv->hdr[PHV_OFF_IPV4_SRC],  4);
    memcpy(key+4, &phv->hdr[PHV_OFF_IPV4_DST],  4);
    memcpy(key+8, &phv->hdr[PHV_OFF_TCP_DPORT], 2);
    *klen = 10;
}

// Stage 2 — L2 FDB：匹配 eth_dst（6 字节，PHV offset 0）
static void extract_s2(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    memcpy(key, &phv->hdr[PHV_OFF_ETH_DST], 6);
    *klen = 6;
}

// Stage 3 — ARP Punt：匹配 eth_type（2 字节，PHV offset 12）
static void extract_s3(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    memcpy(key, &phv->hdr[PHV_OFF_ETH_TYPE], 2);
    *klen = 2;
}

// Stage 4 — VLAN 入口：匹配 [ing_port(1B)] [vlan_tci(2B)]
// vlan_tci 在无标签帧中为 0x0000；TCAM 掩码全 0 表示通配。
static void extract_s4(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    key[0] = phv->ig_port;
    key[1] = phv->hdr[PHV_OFF_VLAN_TCI];
    key[2] = phv->hdr[PHV_OFF_VLAN_TCI + 1];
    *klen = 3;
}

// Stage 5 — DSCP QoS：匹配 ipv4_dscp 字节（高 6 位 = DSCP，低 2 位 = ECN）
// TCAM key = dscp << 2，mask = 0xFC（只比较高 6 位）
static void extract_s5(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    key[0] = phv->hdr[PHV_OFF_IPV4_DSCP];
    *klen = 1;
}

// Stage 6 — VLAN 出口：匹配 [eg_port(1B)] [vlan_id 低字节(1B)]
static void extract_s6(const phv_t *phv, uint8_t *key, uint8_t *klen)
{
    key[0] = phv->eg_port;
    key[1] = (uint8_t)(phv->vlan_id & 0xFF);
    *klen = 2;
}

typedef void (*extract_fn_t)(const phv_t *, uint8_t *, uint8_t *);

static const extract_fn_t stage_extract[PKT_NUM_STAGES] = {
    extract_s0,   // Stage 0: IPv4 LPM
    extract_s1,   // Stage 1: ACL
    extract_s2,   // Stage 2: L2 FDB
    extract_s3,   // Stage 3: ARP Punt
    extract_s4,   // Stage 4: VLAN 入口
    extract_s5,   // Stage 5: DSCP QoS
    extract_s6,   // Stage 6: VLAN 出口
};

// ─────────────────────────────────────────────
// 内部：Action 执行
// ─────────────────────────────────────────────
// 将命中 TCAM 条目的 action_id + action_params 应用到 PHV 上。

static void apply_action(phv_t *phv, const sim_tcam_rec_t *r)
{
    const tcam_entry_t *e = &r->entry;

    switch (e->action_id) {

    // ── IPv4 路由转发 ─────────────────────────
    case ACTION_FORWARD:
        // 设置出端口，并改写 PHV 中的目标 MAC
        phv->eg_port = e->action_params[0];
        memcpy(&phv->hdr[PHV_OFF_ETH_DST], &e->action_params[1], 6);
        break;

    case ACTION_DROP:
        phv->drop = 1;
        break;

    // ── ACL ───────────────────────────────────
    case ACTION_PERMIT:
        // 显式放行：无操作，继续后续 Stage
        break;

    case ACTION_DENY:
        phv->drop = 1;
        break;

    // ── L2 FDB ────────────────────────────────
    case ACTION_L2_FORWARD:
        phv->eg_port = e->action_params[0];
        break;

    case ACTION_FLOOD:
        phv->eg_port = 0xFF;   // 0xFF 表示广播/泛洪
        break;

    // ── ARP Punt ──────────────────────────────
    case ACTION_PUNT_CPU:
        phv->punt = 1;
        break;

    // ── VLAN 入口 ─────────────────────────────
    case ACTION_VLAN_ASSIGN_PVID:
        // action_params[0:1] = PVID（大端 2 字节）
        phv->vlan_id = ((uint16_t)e->action_params[0] << 8)
                     |  (uint16_t)e->action_params[1];
        break;

    case ACTION_VLAN_ACCEPT_TAGGED:
        if (e->action_params[0] == 0 && e->action_params[1] == 0) {
            // Trunk 模式：从 vlan_tci 中直接提取 VID（低 12 位）
            phv->vlan_id = ((uint16_t)(phv->hdr[PHV_OFF_VLAN_TCI] & 0x0F) << 8)
                         |  (uint16_t) phv->hdr[PHV_OFF_VLAN_TCI + 1];
        } else {
            phv->vlan_id = ((uint16_t)e->action_params[0] << 8)
                         |  (uint16_t)e->action_params[1];
        }
        break;

    case ACTION_VLAN_DROP:
        phv->drop = 1;
        break;

    // ── VLAN 出口 ─────────────────────────────
    case ACTION_VLAN_STRIP_TAG:
        phv->vlan_action = VLAN_ACT_STRIP;
        // 清除 PHV 中的 vlan_tci（模拟出口剥离标签）
        phv->hdr[PHV_OFF_VLAN_TCI]     = 0;
        phv->hdr[PHV_OFF_VLAN_TCI + 1] = 0;
        break;

    case ACTION_VLAN_KEEP_TAG:
        phv->vlan_action = VLAN_ACT_KEEP;
        break;

    // ── DSCP QoS ──────────────────────────────
    case ACTION_SET_PRIO:
        phv->qos_prio = e->action_params[0];
        break;

    default:
        // 未知 Action：忽略（保守策略）
        break;
    }
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

int pkt_parse(const uint8_t *raw, uint16_t raw_len,
              uint8_t ing_port, phv_t *phv)
{
    if (!raw || !phv || raw_len < 14) return -1;

    memset(phv, 0, sizeof(*phv));
    phv->ig_port = ing_port;

    // ── 以太网头（固定 14 字节）────────────────
    memcpy(&phv->hdr[PHV_OFF_ETH_DST], raw,     6);   // dst MAC
    memcpy(&phv->hdr[PHV_OFF_ETH_SRC], raw + 6, 6);   // src MAC
    phv->hdr[PHV_OFF_ETH_TYPE]     = raw[12];
    phv->hdr[PHV_OFF_ETH_TYPE + 1] = raw[13];

    uint16_t eth_type = ((uint16_t)raw[12] << 8) | raw[13];
    int      ip_start = 14;

    // ── 802.1Q VLAN 标签（可选）──────────────
    if (eth_type == 0x8100 && raw_len >= 18) {
        phv->hdr[PHV_OFF_VLAN_TCI]     = raw[14];
        phv->hdr[PHV_OFF_VLAN_TCI + 1] = raw[15];
        phv->vlan_id = ((uint16_t)(raw[14] & 0x0F) << 8) | raw[15];
        eth_type  = ((uint16_t)raw[16] << 8) | raw[17];
        ip_start  = 18;
    }

    // ── IPv4 报头（可选）─────────────────────
    if (eth_type == 0x0800 && raw_len >= (uint16_t)(ip_start + 20)) {
        const uint8_t *ip = raw + ip_start;

        phv->hdr[PHV_OFF_IPV4_VER_IHL]     = ip[0];
        phv->hdr[PHV_OFF_IPV4_DSCP]        = ip[1];   // TOS（DSCP + ECN）
        phv->hdr[PHV_OFF_IPV4_TOT_LEN]     = ip[2];
        phv->hdr[PHV_OFF_IPV4_TOT_LEN + 1] = ip[3];
        phv->hdr[PHV_OFF_IPV4_TTL]         = ip[8];
        phv->hdr[PHV_OFF_IPV4_PROTO]       = ip[9];
        memcpy(&phv->hdr[PHV_OFF_IPV4_SRC], ip + 12, 4);
        memcpy(&phv->hdr[PHV_OFF_IPV4_DST], ip + 16, 4);

        // ── TCP/UDP 端口（可选）──────────────
        int ihl = (ip[0] & 0x0F) * 4;
        uint8_t proto = ip[9];
        if ((proto == 6 || proto == 17) &&
            raw_len >= (uint16_t)(ip_start + ihl + 4)) {
            const uint8_t *l4 = ip + ihl;
            phv->hdr[PHV_OFF_TCP_SPORT]     = l4[0];
            phv->hdr[PHV_OFF_TCP_SPORT + 1] = l4[1];
            phv->hdr[PHV_OFF_TCP_DPORT]     = l4[2];
            phv->hdr[PHV_OFF_TCP_DPORT + 1] = l4[3];
        }
    }

    return 0;
}

int pkt_forward(phv_t *phv, fwd_result_t *result)
{
    if (!phv || !result) return -1;

    for (int stage = 0; stage < PKT_NUM_STAGES; stage++) {
        // 一旦确定丢弃或上送 CPU，退出流水线
        if (phv->drop || phv->punt) break;

        // 提取本级匹配键
        uint8_t key[64];
        uint8_t key_len = 0;
        memset(key, 0, sizeof(key));
        stage_extract[stage](phv, key, &key_len);

        // 三值 TCAM 查找
        sim_tcam_rec_t *m = tcam_ternary_lookup((uint8_t)stage, key, key_len);
        if (!m) continue;   // 未命中：本级透传，PHV 不变

        // 执行 Action
        apply_action(phv, m);
    }

    result->eg_port     = phv->eg_port;
    result->drop        = phv->drop;
    result->punt        = phv->punt;
    result->vlan_id     = phv->vlan_id;
    result->qos_prio    = phv->qos_prio;
    result->vlan_action = phv->vlan_action;
    return 0;
}

int pkt_process(const uint8_t *raw, uint16_t raw_len,
                uint8_t ing_port, fwd_result_t *result)
{
    phv_t phv;
    int rc = pkt_parse(raw, raw_len, ing_port, &phv);
    if (rc != 0) return rc;
    return pkt_forward(&phv, result);
}
