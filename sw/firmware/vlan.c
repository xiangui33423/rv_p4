// vlan.c
// VLAN 管理模块实现
// 维护软件 VLAN 数据库，并通过 HAL 向数据面安装 MAU 规则
//
// 数据面规则布局：
//   Stage 4（入口）：(ing_port[7:0], vlan_tci[15:0]) → 分配 meta.vlan_id
//   Stage 6（出口）：(eg_port[7:0],  meta.vlan_id[7:0]) → strip / keep 标签

#include "vlan.h"
#include "table_map.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 软件状态
// ─────────────────────────────────────────────
static vlan_entry_t   vlan_db[VLAN_MAX_ID + 1];
static port_vlan_cfg_t port_cfg[32];

// ─────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────

/* 将 uint16_t 写入 key bytes（大端） */
static void u16_to_key(uint8_t *buf, int off, uint16_t val) {
    buf[off]   = (val >> 8) & 0xFF;
    buf[off+1] = (val     ) & 0xFF;
}

/* 构造并插入一条入口 VLAN TCAM 规则
 *   key : [ing_port(1B)] [vlan_tci(2B)] = 3B
 *   mask: port 精确匹配(0xFF)，vlan_tci 按需屏蔽
 *   action_params[0] = vlan_id（分配给 meta.vlan_id）
 */
static void install_ingress_rule(port_id_t port,
                                  uint16_t vlan_tci_val,
                                  uint16_t vlan_tci_mask,
                                  uint16_t action_id,
                                  uint16_t meta_vlan_id,
                                  uint16_t table_id) {
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    e.key.key_len  = 3;
    e.key.bytes[0] = port;
    u16_to_key(e.key.bytes, 1, vlan_tci_val);

    e.mask.key_len  = 3;
    e.mask.bytes[0] = 0xFF;           // port 精确匹配
    u16_to_key(e.mask.bytes, 1, vlan_tci_mask);

    e.stage     = TABLE_VLAN_INGRESS_STAGE;
    e.table_id  = table_id;
    e.action_id = action_id;
    e.action_params[0] = (uint8_t)((meta_vlan_id >> 8) & 0xFF);
    e.action_params[1] = (uint8_t)(meta_vlan_id & 0xFF);

    hal_tcam_insert(&e);
}

/* 构造并插入一条出口 VLAN TCAM 规则
 *   key : [eg_port(1B)] [vlan_id(1B)] = 2B（vlan_id 限 0-255）
 *   action: strip 或 keep
 */
static void install_egress_rule(port_id_t port,
                                 uint16_t vlan_id,
                                 uint16_t action_id) {
    tcam_entry_t e;
    memset(&e, 0, sizeof(e));

    e.key.key_len   = 2;
    e.key.bytes[0]  = port;
    e.key.bytes[1]  = (uint8_t)(vlan_id & 0xFF);

    e.mask.key_len  = 2;
    e.mask.bytes[0] = 0xFF;
    e.mask.bytes[1] = 0xFF;

    e.stage     = TABLE_VLAN_EGRESS_STAGE;
    e.table_id  = (uint16_t)VLAN_EGRESS_ENTRY(port, vlan_id);
    e.action_id = action_id;

    hal_tcam_insert(&e);
}

// ─────────────────────────────────────────────
// 公共 API 实现
// ─────────────────────────────────────────────

void vlan_init(void) {
    memset(vlan_db,   0, sizeof(vlan_db));
    memset(port_cfg,  0, sizeof(port_cfg));

    // 所有端口默认 access 模式，PVID=1
    for (int p = 0; p < 32; p++) {
        port_cfg[p].pvid = VLAN_DEFAULT_ID;
        port_cfg[p].mode = VLAN_MODE_ACCESS;
        hal_vlan_pvid_set((port_id_t)p, VLAN_DEFAULT_ID);
        hal_vlan_mode_set((port_id_t)p, VLAN_MODE_ACCESS);
    }

    // 创建默认 VLAN 1，所有端口加入（无标签）
    vlan_create(VLAN_DEFAULT_ID);
    for (int p = 0; p < 32; p++)
        vlan_port_add(VLAN_DEFAULT_ID, (port_id_t)p, 0);
}

int vlan_create(uint16_t vlan_id) {
    if (vlan_id == 0 || vlan_id > VLAN_MAX_ID) return HAL_ERR_INVAL;
    if (vlan_db[vlan_id].valid)                 return HAL_OK;  // 已存在

    vlan_db[vlan_id].member_bitmap   = 0;
    vlan_db[vlan_id].untagged_bitmap = 0;
    vlan_db[vlan_id].valid           = 1;

    hal_vlan_member_set(vlan_id, 0, 0);
    return HAL_OK;
}

int vlan_delete(uint16_t vlan_id) {
    if (vlan_id == 0 || vlan_id > VLAN_MAX_ID) return HAL_ERR_INVAL;
    if (!vlan_db[vlan_id].valid)                return HAL_ERR_INVAL;

    // 将每个成员端口从该 VLAN 移除并删除出口规则
    uint32_t mbr = vlan_db[vlan_id].member_bitmap;
    for (int p = 0; p < 32; p++) {
        if (mbr & (1U << p))
            hal_tcam_delete(TABLE_VLAN_EGRESS_STAGE,
                            (uint16_t)VLAN_EGRESS_ENTRY(p, vlan_id));
    }

    memset(&vlan_db[vlan_id], 0, sizeof(vlan_entry_t));
    hal_vlan_member_set(vlan_id, 0, 0);
    return HAL_OK;
}

int vlan_port_add(uint16_t vlan_id, port_id_t port, uint8_t tagged) {
    if (vlan_id == 0 || vlan_id > VLAN_MAX_ID) return HAL_ERR_INVAL;
    if (port >= 32)                             return HAL_ERR_INVAL;
    if (!vlan_db[vlan_id].valid)                return HAL_ERR_INVAL;

    vlan_db[vlan_id].member_bitmap |= (1U << port);
    if (!tagged)
        vlan_db[vlan_id].untagged_bitmap |= (1U << port);
    else
        vlan_db[vlan_id].untagged_bitmap &= ~(1U << port);

    hal_vlan_member_set(vlan_id,
                        vlan_db[vlan_id].member_bitmap,
                        vlan_db[vlan_id].untagged_bitmap);

    // 安装出口规则
    uint16_t eg_action = tagged ? ACTION_VLAN_KEEP_TAG : ACTION_VLAN_STRIP_TAG;
    install_egress_rule(port, vlan_id, eg_action);

    return HAL_OK;
}

int vlan_port_remove(uint16_t vlan_id, port_id_t port) {
    if (vlan_id == 0 || vlan_id > VLAN_MAX_ID) return HAL_ERR_INVAL;
    if (port >= 32)                             return HAL_ERR_INVAL;
    if (!vlan_db[vlan_id].valid)                return HAL_ERR_INVAL;

    vlan_db[vlan_id].member_bitmap   &= ~(1U << port);
    vlan_db[vlan_id].untagged_bitmap &= ~(1U << port);

    hal_vlan_member_set(vlan_id,
                        vlan_db[vlan_id].member_bitmap,
                        vlan_db[vlan_id].untagged_bitmap);

    hal_tcam_delete(TABLE_VLAN_EGRESS_STAGE,
                    (uint16_t)VLAN_EGRESS_ENTRY(port, vlan_id));
    return HAL_OK;
}

int vlan_port_set_pvid(port_id_t port, uint16_t vlan_id) {
    if (port >= 32 || vlan_id == 0 || vlan_id > VLAN_MAX_ID)
        return HAL_ERR_INVAL;

    port_cfg[port].pvid = vlan_id;
    hal_vlan_pvid_set(port, vlan_id);

    // 重新安装该端口的入口规则（PVID 改变）
    vlan_install_port_rules(port);
    return HAL_OK;
}

int vlan_port_set_mode(port_id_t port, uint8_t mode) {
    if (port >= 32 || (mode != VLAN_MODE_ACCESS && mode != VLAN_MODE_TRUNK))
        return HAL_ERR_INVAL;

    port_cfg[port].mode = mode;
    hal_vlan_mode_set(port, mode);

    vlan_install_port_rules(port);
    return HAL_OK;
}

void vlan_install_port_rules(port_id_t port) {
    uint16_t pvid = port_cfg[port].pvid;
    uint8_t  mode = port_cfg[port].mode;

    if (mode == VLAN_MODE_ACCESS) {
        /*
         * 入口规则 1（无标签帧）：
         *   key:  [port, vlan_tci=0x0000]  mask: [0xFF, 0xFFFF]
         *   → ACTION_VLAN_ASSIGN_PVID，meta.vlan_id = pvid
         *   TCAM entry index: VLAN_INGRESS_ENTRY(port, 0)
         */
        install_ingress_rule(port,
                             0x0000,         // vlan_tci 值：无标签 EtherType 不含 TCI
                             0x0000,         // 掩码 0：通配（匹配所有无标签帧）
                             ACTION_VLAN_ASSIGN_PVID,
                             pvid,
                             (uint16_t)VLAN_INGRESS_ENTRY(port, 0));
        /*
         * 入口规则 2（带标签帧）：
         *   key:  [port, vlan_tci=pvid<<4]  mask: [0xFF, 0x0FFF]（忽略 PCP/DEI）
         *   → ACTION_VLAN_ACCEPT_TAGGED
         *   TCAM entry index: VLAN_INGRESS_ENTRY(port, 1)
         */
        install_ingress_rule(port,
                             (uint16_t)(pvid & 0x0FFF),  // VID 部分
                             0x0FFF,                      // 只匹配 VID
                             ACTION_VLAN_ACCEPT_TAGGED,
                             pvid,
                             (uint16_t)VLAN_INGRESS_ENTRY(port, 1));
    } else {
        /*
         * trunk 模式：
         *   规则 1（无标签帧）：→ 赋 PVID
         *   规则 2（任意带标签帧）：→ 接受（通配 vlan_tci）
         */
        install_ingress_rule(port,
                             0x0000, 0x0000,
                             ACTION_VLAN_ASSIGN_PVID,
                             pvid,
                             (uint16_t)VLAN_INGRESS_ENTRY(port, 0));
        install_ingress_rule(port,
                             0x0000,          // key 通配
                             0x0000,          // mask 全 0：匹配任意标签
                             ACTION_VLAN_ACCEPT_TAGGED,
                             0,               // 由数据面从 vlan_tci 直接提取
                             (uint16_t)VLAN_INGRESS_ENTRY(port, 1));
    }
}

void vlan_show(void) {
    printf("=== VLAN Database ===\n");
    for (int v = 1; v <= VLAN_MAX_ID; v++) {
        if (!vlan_db[v].valid) continue;
        printf("VLAN %3d  members=0x%08X  untagged=0x%08X\n",
               v, vlan_db[v].member_bitmap, vlan_db[v].untagged_bitmap);
    }
    printf("=== Port VLAN Config ===\n");
    for (int p = 0; p < 32; p++) {
        printf("  Port%2d  PVID=%-4u  mode=%s\n",
               p, port_cfg[p].pvid,
               port_cfg[p].mode == VLAN_MODE_ACCESS ? "access" : "trunk");
    }
}
