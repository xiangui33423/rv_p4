// fdb.c
// L2 FDB 管理实现
// 软件表：256 槽线性扫描；TCAM：Stage 2，table_id = dmac & 0xFFF

#include "fdb.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 软件状态
// ─────────────────────────────────────────────
static fdb_entry_t fdb_table[FDB_TABLE_SIZE];

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

static fdb_entry_t *fdb_find(uint64_t dmac) {
    for (int i = 0; i < FDB_TABLE_SIZE; i++) {
        if (fdb_table[i].valid && fdb_table[i].dmac == dmac)
            return &fdb_table[i];
    }
    return NULL;
}

static fdb_entry_t *fdb_alloc(void) {
    for (int i = 0; i < FDB_TABLE_SIZE; i++) {
        if (!fdb_table[i].valid)
            return &fdb_table[i];
    }
    return NULL;
}

/* 将 MAC→port 规则写入 Stage 2 TCAM（精确匹配） */
static int fdb_install_tcam(uint64_t dmac, uint8_t port) {
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    e.key.key_len   = 6;
    e.key.bytes[0]  = (uint8_t)((dmac >> 40) & 0xFF);
    e.key.bytes[1]  = (uint8_t)((dmac >> 32) & 0xFF);
    e.key.bytes[2]  = (uint8_t)((dmac >> 24) & 0xFF);
    e.key.bytes[3]  = (uint8_t)((dmac >> 16) & 0xFF);
    e.key.bytes[4]  = (uint8_t)((dmac >>  8) & 0xFF);
    e.key.bytes[5]  = (uint8_t)((dmac >>  0) & 0xFF);

    e.mask.key_len  = 6;
    memset(e.mask.bytes, 0xFF, 6);   // 精确匹配全部 6 字节

    e.stage             = TABLE_L2_FDB_STAGE;
    e.table_id          = TABLE_L2_FDB_BASE + (uint16_t)(dmac & 0xFFF);
    e.action_id         = ACTION_L2_FORWARD;
    e.action_params[0]  = port;

    return hal_tcam_insert(&e);
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void fdb_init(void) {
    memset(fdb_table, 0, sizeof(fdb_table));
}

int fdb_learn(uint64_t dmac, uint8_t port) {
    fdb_entry_t *e = fdb_find(dmac);
    if (e) {
        /* 已存在：更新端口并刷新 age */
        e->port      = port;
        e->age_ticks = 0;
    } else {
        e = fdb_alloc();
        if (!e) return HAL_ERR_FULL;
        e->dmac      = dmac;
        e->port      = port;
        e->vlan      = 0;
        e->age_ticks = 0;
        e->is_static = 0;
        e->valid     = 1;
    }
    return fdb_install_tcam(dmac, port);
}

int fdb_add_static(uint64_t dmac, uint8_t port, uint16_t vlan) {
    fdb_entry_t *e = fdb_find(dmac);
    if (!e) {
        e = fdb_alloc();
        if (!e) return HAL_ERR_FULL;
    }
    e->dmac      = dmac;
    e->port      = port;
    e->vlan      = vlan;
    e->age_ticks = 0;
    e->is_static = 1;
    e->valid     = 1;
    return fdb_install_tcam(dmac, port);
}

int fdb_delete(uint64_t dmac) {
    fdb_entry_t *e = fdb_find(dmac);
    if (!e) return HAL_ERR_INVAL;

    uint16_t tid = TABLE_L2_FDB_BASE + (uint16_t)(dmac & 0xFFF);
    hal_tcam_delete(TABLE_L2_FDB_STAGE, tid);
    memset(e, 0, sizeof(*e));
    return HAL_OK;
}

void fdb_age(uint32_t now_sec) {
    for (int i = 0; i < FDB_TABLE_SIZE; i++) {
        fdb_entry_t *e = &fdb_table[i];
        if (!e->valid || e->is_static) continue;
        if ((now_sec - e->age_ticks) >= FDB_AGE_DYNAMIC) {
            uint64_t dmac = e->dmac;   // save before memset
            uint16_t tid  = TABLE_L2_FDB_BASE + (uint16_t)(dmac & 0xFFF);
            hal_tcam_delete(TABLE_L2_FDB_STAGE, tid);
            memset(e, 0, sizeof(*e));
        }
    }
}

void fdb_show(void) {
    printf("%-20s  %-5s  %-6s  %-7s\n", "MAC", "Port", "VLAN", "Type");
    printf("────────────────────────────────────────────\n");
    int found = 0;
    for (int i = 0; i < FDB_TABLE_SIZE; i++) {
        fdb_entry_t *e = &fdb_table[i];
        if (!e->valid) continue;
        found++;
        uint64_t m = e->dmac;
        printf("%02x:%02x:%02x:%02x:%02x:%02x   %-5d  %-6d  %s\n",
               (unsigned)((m >> 40) & 0xFF), (unsigned)((m >> 32) & 0xFF),
               (unsigned)((m >> 24) & 0xFF), (unsigned)((m >> 16) & 0xFF),
               (unsigned)((m >>  8) & 0xFF), (unsigned)((m >>  0) & 0xFF),
               e->port, e->vlan,
               e->is_static ? "static" : "dynamic");
    }
    if (!found) printf("(empty)\n");
}
