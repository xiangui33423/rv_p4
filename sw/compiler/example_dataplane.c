/*
 * example_dataplane.c
 * RV-P4 数据面示例程序
 * 演示：Parser + IPv4 LPM + ACL + L2 FDB
 *
 * 编译：python3 rvp4cc.py example_dataplane.c -o dataplane.hwcfg --report
 */

#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────
 * PHV 字段定义
 * ───────────────────────────────────────────── */
typedef struct {
    uint8_t  eth_dst[6]   __attribute__((rvp4_phv_field));
    uint8_t  eth_src[6]   __attribute__((rvp4_phv_field));
    uint16_t eth_type     __attribute__((rvp4_phv_field));
    uint8_t  ipv4_ttl     __attribute__((rvp4_phv_field));
    uint8_t  ipv4_proto   __attribute__((rvp4_phv_field));
    uint32_t ipv4_src     __attribute__((rvp4_phv_field));
    uint32_t ipv4_dst     __attribute__((rvp4_phv_field));
    uint16_t tcp_dport    __attribute__((rvp4_phv_field));
    uint16_t udp_dport    __attribute__((rvp4_phv_field));
} phv_t;

typedef struct {
    uint8_t  ig_port      __attribute__((rvp4_metadata));
    uint8_t  eg_port      __attribute__((rvp4_metadata));
    bool     drop         __attribute__((rvp4_metadata));
    uint8_t  qos_prio     __attribute__((rvp4_metadata));
} metadata_t;

/* ─────────────────────────────────────────────
 * Parser 状态函数
 * ───────────────────────────────────────────── */

/* 解析以太网帧头 */
__attribute__((rvp4_parser))
void parse_ethernet(phv_t *phv, metadata_t *meta) {
    /* 提取 eth_dst, eth_src, eth_type → PHV[0..13] */
    /* 根据 eth_type 跳转到下一状态 */
    if (phv->eth_type == 0x0800) {
        /* → parse_ipv4 */
    }
}

/* 解析 IPv4 头 */
__attribute__((rvp4_parser))
void parse_ipv4(phv_t *phv, metadata_t *meta) {
    /* 提取 ipv4_ttl, ipv4_proto, ipv4_src, ipv4_dst → PHV[26..37] */
    if (phv->ipv4_proto == 6) {
        /* → parse_tcp */
    } else if (phv->ipv4_proto == 17) {
        /* → parse_udp */
    }
}

/* 解析 TCP 头 */
__attribute__((rvp4_parser))
void parse_tcp(phv_t *phv, metadata_t *meta) {
    /* 提取 tcp_dport → PHV[40..41] */
}

/* 解析 UDP 头 */
__attribute__((rvp4_parser))
void parse_udp(phv_t *phv, metadata_t *meta) {
    /* 提取 udp_dport → PHV[40..41] */
}

/* ─────────────────────────────────────────────
 * Action 函数
 * ───────────────────────────────────────────── */

/* 转发：设置出端口 + 目的 MAC */
__attribute__((rvp4_action))
void action_forward(metadata_t *meta, uint8_t port) {
    meta->eg_port = port;
}

/* 丢弃 */
__attribute__((rvp4_action))
void action_drop(metadata_t *meta) {
    meta->drop = true;
}

/* ACL 放行 */
__attribute__((rvp4_action))
void action_permit(metadata_t *meta) {
    /* NOP */
}

/* ACL 拒绝 */
__attribute__((rvp4_action))
void action_deny(metadata_t *meta) {
    meta->drop = true;
}

/* L2 转发 */
__attribute__((rvp4_action))
void action_l2_forward(metadata_t *meta, uint8_t port) {
    meta->eg_port = port;
}

/* 泛洪 */
__attribute__((rvp4_action))
void action_flood(metadata_t *meta) {
    /* 设置组播标志 */
}

/* TTL 递减 */
__attribute__((rvp4_action))
void action_ttl_dec(phv_t *phv) {
    phv->ipv4_ttl -= 1;
}

/* ─────────────────────────────────────────────
 * 表定义
 * ───────────────────────────────────────────── */

/*
 * IPv4 LPM 路由表
 * 匹配：ipv4_dst（LPM）
 * 动作：forward / drop
 * Stage 0
 */
__attribute__((rvp4_table))
__attribute__((rvp4_lpm))
__attribute__((rvp4_stage(0)))
__attribute__((rvp4_size(65536)))
void table_ipv4_lpm(phv_t *phv, metadata_t *meta) {
    /* key: ipv4_dst */
    /* actions: action_forward, action_drop */
}

/*
 * ACL 入方向表
 * 匹配：ipv4_src + ipv4_dst + tcp_dport（三值匹配）
 * 动作：permit / deny
 * Stage 1
 */
__attribute__((rvp4_table))
__attribute__((rvp4_ternary))
__attribute__((rvp4_stage(1)))
__attribute__((rvp4_size(4096)))
void table_acl_ingress(phv_t *phv, metadata_t *meta) {
    /* key: ipv4_src, ipv4_dst, tcp_dport */
    /* actions: action_permit, action_deny */
}

/*
 * L2 FDB 表
 * 匹配：eth_dst（精确匹配）
 * 动作：l2_forward / flood
 * Stage 2
 */
__attribute__((rvp4_table))
__attribute__((rvp4_exact))
__attribute__((rvp4_stage(2)))
__attribute__((rvp4_size(32768)))
void table_l2_fdb(phv_t *phv, metadata_t *meta) {
    /* key: eth_dst */
    /* actions: action_l2_forward, action_flood */
}

/*
 * QoS 标记表
 * 匹配：ipv4_dscp（精确匹配）
 * 动作：set_prio
 * Stage 3
 */
__attribute__((rvp4_table))
__attribute__((rvp4_exact))
__attribute__((rvp4_stage(3)))
__attribute__((rvp4_size(64)))
void table_qos_mark(phv_t *phv, metadata_t *meta) {
    /* key: ipv4_dscp */
    /* actions: action_set_prio */
}
