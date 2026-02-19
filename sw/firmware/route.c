// route.c
// IPv4 LPM 路由管理实现

#include "route.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 内部数据结构
// ─────────────────────────────────────────────
typedef struct {
    uint32_t  prefix;
    uint8_t   len;
    uint8_t   port;
    uint64_t  dmac;
    uint8_t   valid;
} route_entry_t;

static route_entry_t route_table[ROUTE_TABLE_SIZE];

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

static uint32_t prefix_to_mask(uint8_t len) {
    if (len == 0)  return 0x00000000U;
    if (len >= 32) return 0xFFFFFFFFU;
    return ~((1U << (32 - len)) - 1U);
}

static void u32_to_key(uint8_t *buf, int off, uint32_t val) {
    buf[off + 0] = (uint8_t)((val >> 24) & 0xFF);
    buf[off + 1] = (uint8_t)((val >> 16) & 0xFF);
    buf[off + 2] = (uint8_t)((val >>  8) & 0xFF);
    buf[off + 3] = (uint8_t)((val >>  0) & 0xFF);
}

/* TCAM table_id for a given prefix/len */
static uint16_t route_tcam_id(uint32_t prefix, uint8_t len) {
    if (len == 0) return (uint16_t)TABLE_IPV4_LPM_BASE;
    return (uint16_t)(TABLE_IPV4_LPM_BASE + (prefix >> (32 - len)));
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void route_init(void) {
    memset(route_table, 0, sizeof(route_table));
}

int route_add(uint32_t prefix, uint8_t len, uint8_t port, uint64_t dmac) {
    if (len > 32) return HAL_ERR_INVAL;

    /* 更新软件表：优先找已有相同 prefix/len 的条目，否则找空槽 */
    route_entry_t *slot = NULL;
    for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
        if (route_table[i].valid &&
            route_table[i].prefix == prefix &&
            route_table[i].len    == len) {
            slot = &route_table[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
            if (!route_table[i].valid) {
                slot = &route_table[i];
                break;
            }
        }
    }
    if (!slot) return HAL_ERR_FULL;

    slot->prefix = prefix;
    slot->len    = len;
    slot->port   = port;
    slot->dmac   = dmac;
    slot->valid  = 1;

    /* 安装 TCAM 规则 */
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    e.key.key_len  = 4;
    u32_to_key(e.key.bytes, 0, prefix);

    e.mask.key_len = 4;
    u32_to_key(e.mask.bytes, 0, prefix_to_mask(len));

    e.stage    = TABLE_IPV4_LPM_STAGE;
    e.table_id = route_tcam_id(prefix, len);
    e.action_id = ACTION_FORWARD;

    e.action_params[0] = port;
    e.action_params[1] = (uint8_t)((dmac >> 40) & 0xFF);
    e.action_params[2] = (uint8_t)((dmac >> 32) & 0xFF);
    e.action_params[3] = (uint8_t)((dmac >> 24) & 0xFF);
    e.action_params[4] = (uint8_t)((dmac >> 16) & 0xFF);
    e.action_params[5] = (uint8_t)((dmac >>  8) & 0xFF);
    e.action_params[6] = (uint8_t)((dmac >>  0) & 0xFF);

    return hal_tcam_insert(&e);
}

int route_del(uint32_t prefix, uint8_t len) {
    if (len > 32) return HAL_ERR_INVAL;

    for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
        if (route_table[i].valid &&
            route_table[i].prefix == prefix &&
            route_table[i].len    == len) {
            route_table[i].valid = 0;
            break;
        }
    }
    return hal_tcam_delete(TABLE_IPV4_LPM_STAGE, route_tcam_id(prefix, len));
}

void route_show(void) {
    printf("%-20s  %-5s  %-17s\n", "Prefix/Len", "Port", "Next-Hop MAC");
    printf("────────────────────────────────────────────────\n");
    int found = 0;
    for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
        route_entry_t *e = &route_table[i];
        if (!e->valid) continue;
        found++;
        uint8_t a = (uint8_t)((e->prefix >> 24) & 0xFF);
        uint8_t b = (uint8_t)((e->prefix >> 16) & 0xFF);
        uint8_t c = (uint8_t)((e->prefix >>  8) & 0xFF);
        uint8_t d = (uint8_t)((e->prefix >>  0) & 0xFF);
        uint64_t m = e->dmac;
        printf("%u.%u.%u.%u/%-3u       %-5u  %02x:%02x:%02x:%02x:%02x:%02x\n",
               a, b, c, d, e->len, e->port,
               (unsigned)((m >> 40) & 0xFF), (unsigned)((m >> 32) & 0xFF),
               (unsigned)((m >> 24) & 0xFF), (unsigned)((m >> 16) & 0xFF),
               (unsigned)((m >>  8) & 0xFF), (unsigned)((m >>  0) & 0xFF));
    }
    if (!found) printf("(empty)\n");
}
