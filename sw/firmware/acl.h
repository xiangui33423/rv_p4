// acl.h
// ACL 规则管理模块
// 支持 deny / permit 规则，按序列 ID 管理，向 Stage 1 TCAM 安装

#ifndef ACL_H
#define ACL_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define ACL_TABLE_SIZE  128     // 最大 ACL 规则数

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * acl_init - 清空 ACL 软件状态，重置规则 ID 计数器
 */
void acl_init(void);

/**
 * acl_add_deny - 添加拒绝规则
 * @src_ip/@src_mask: 源 IP 及掩码（0 = 通配）
 * @dst_ip/@dst_mask: 目的 IP 及掩码（0 = 通配）
 * @dport:            目的端口（0 = 通配）
 * 返回：分配的规则 ID（>= 0）或 HAL_ERR_FULL(-2) / HAL_ERR_BUSY(-1)
 */
int acl_add_deny(uint32_t src_ip, uint32_t src_mask,
                 uint32_t dst_ip, uint32_t dst_mask,
                 uint16_t dport);

/**
 * acl_add_permit - 添加放行规则（key 仅 src+dst IP，无端口字段）
 * 返回：分配的规则 ID（>= 0）或 HAL_ERR_FULL(-2)
 */
int acl_add_permit(uint32_t src_ip, uint32_t src_mask,
                   uint32_t dst_ip, uint32_t dst_mask);

/**
 * acl_delete - 按规则 ID 删除规则，并从 TCAM 撤销
 * 返回 HAL_OK 或 HAL_ERR_INVAL（ID 不存在）
 */
int acl_delete(uint16_t rule_id);

/**
 * acl_show - 打印 ACL 规则表（调试 / CLI show acl）
 */
void acl_show(void);

#endif /* ACL_H */
