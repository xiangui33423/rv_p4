// acl.c
// ACL 规则管理实现

#include "acl.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 内部数据结构
// ─────────────────────────────────────────────
typedef struct {
    uint32_t  src_ip,  src_mask;
    uint32_t  dst_ip,  dst_mask;
    uint16_t  dport;         // 0 = 通配
    uint8_t   action;        // 0=deny, 1=permit
    uint8_t   valid;
    uint16_t  rule_id;       // 分配的规则 ID（TCAM table_id offset）
} acl_entry_t;

static acl_entry_t acl_table[ACL_TABLE_SIZE];
static uint16_t    acl_next_id;   // 单调递增规则 ID

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

static void u32_to_key(uint8_t *buf, int off, uint32_t val) {
    buf[off + 0] = (uint8_t)((val >> 24) & 0xFF);
    buf[off + 1] = (uint8_t)((val >> 16) & 0xFF);
    buf[off + 2] = (uint8_t)((val >>  8) & 0xFF);
    buf[off + 3] = (uint8_t)((val >>  0) & 0xFF);
}

static acl_entry_t *acl_alloc(void) {
    for (int i = 0; i < ACL_TABLE_SIZE; i++) {
        if (!acl_table[i].valid)
            return &acl_table[i];
    }
    return NULL;
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void acl_init(void) {
    memset(acl_table, 0, sizeof(acl_table));
    acl_next_id = 0;
}

int acl_add_deny(uint32_t src_ip, uint32_t src_mask,
                 uint32_t dst_ip, uint32_t dst_mask,
                 uint16_t dport) {
    acl_entry_t *e = acl_alloc();
    if (!e) return HAL_ERR_FULL;

    e->src_ip   = src_ip;
    e->src_mask = src_mask;
    e->dst_ip   = dst_ip;
    e->dst_mask = dst_mask;
    e->dport    = dport;
    e->action   = 0;
    e->valid    = 1;
    e->rule_id  = acl_next_id++;

    tcam_entry_t te;
    memset(&te, 0, sizeof(te));

    te.key.key_len  = 10;
    u32_to_key(te.key.bytes, 0, src_ip);
    u32_to_key(te.key.bytes, 4, dst_ip);
    te.key.bytes[8]  = (uint8_t)((dport >> 8) & 0xFF);
    te.key.bytes[9]  = (uint8_t)((dport     ) & 0xFF);

    te.mask.key_len = 10;
    u32_to_key(te.mask.bytes, 0, src_mask);
    u32_to_key(te.mask.bytes, 4, dst_mask);
    if (dport) {
        te.mask.bytes[8] = 0xFF;
        te.mask.bytes[9] = 0xFF;
    }

    te.stage    = TABLE_ACL_INGRESS_STAGE;
    te.table_id = (uint16_t)(TABLE_ACL_INGRESS_BASE + e->rule_id);
    te.action_id = ACTION_DENY;

    int ret = hal_tcam_insert(&te);
    if (ret != HAL_OK) { e->valid = 0; return ret; }
    return (int)e->rule_id;   /* 成功：返回分配的规则 ID */
}

int acl_add_permit(uint32_t src_ip, uint32_t src_mask,
                   uint32_t dst_ip, uint32_t dst_mask) {
    acl_entry_t *e = acl_alloc();
    if (!e) return HAL_ERR_FULL;

    e->src_ip   = src_ip;
    e->src_mask = src_mask;
    e->dst_ip   = dst_ip;
    e->dst_mask = dst_mask;
    e->dport    = 0;
    e->action   = 1;
    e->valid    = 1;
    e->rule_id  = acl_next_id++;

    tcam_entry_t te;
    memset(&te, 0, sizeof(te));

    te.key.key_len  = 8;
    u32_to_key(te.key.bytes, 0, src_ip);
    u32_to_key(te.key.bytes, 4, dst_ip);

    te.mask.key_len = 8;
    u32_to_key(te.mask.bytes, 0, src_mask);
    u32_to_key(te.mask.bytes, 4, dst_mask);

    te.stage    = TABLE_ACL_INGRESS_STAGE;
    te.table_id = (uint16_t)(TABLE_ACL_INGRESS_BASE + e->rule_id);
    te.action_id = ACTION_PERMIT;

    int ret = hal_tcam_insert(&te);
    if (ret != HAL_OK) { e->valid = 0; return ret; }
    return (int)e->rule_id;   /* 成功：返回分配的规则 ID */
}

int acl_delete(uint16_t rule_id) {
    for (int i = 0; i < ACL_TABLE_SIZE; i++) {
        if (acl_table[i].valid && acl_table[i].rule_id == rule_id) {
            acl_table[i].valid = 0;
            return hal_tcam_delete(TABLE_ACL_INGRESS_STAGE,
                                   (uint16_t)(TABLE_ACL_INGRESS_BASE + rule_id));
        }
    }
    return HAL_ERR_INVAL;
}

void acl_show(void) {
    static const char *act[] = {"deny", "permit"};
    printf("%-5s  %-20s  %-20s  %-6s  %-8s\n",
           "ID", "Src-IP/Mask", "Dst-IP/Mask", "DPort", "Action");
    printf("────────────────────────────────────────────────────────────\n");
    int found = 0;
    for (int i = 0; i < ACL_TABLE_SIZE; i++) {
        acl_entry_t *e = &acl_table[i];
        if (!e->valid) continue;
        found++;
        printf("%-5u  %u.%u.%u.%u/%u.%u.%u.%u  "
               "%u.%u.%u.%u/%u.%u.%u.%u  %-6u  %s\n",
               e->rule_id,
               (e->src_ip>>24)&0xFF, (e->src_ip>>16)&0xFF,
               (e->src_ip>> 8)&0xFF,  e->src_ip     &0xFF,
               (e->src_mask>>24)&0xFF,(e->src_mask>>16)&0xFF,
               (e->src_mask>> 8)&0xFF, e->src_mask   &0xFF,
               (e->dst_ip>>24)&0xFF, (e->dst_ip>>16)&0xFF,
               (e->dst_ip>> 8)&0xFF,  e->dst_ip     &0xFF,
               (e->dst_mask>>24)&0xFF,(e->dst_mask>>16)&0xFF,
               (e->dst_mask>> 8)&0xFF, e->dst_mask   &0xFF,
               e->dport, act[e->action & 1]);
    }
    if (!found) printf("(empty)\n");
}
