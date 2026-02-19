// arp.h
// ARP 协议 + 邻居表管理模块
// 运行在香山 RISC-V 裸机固件上
//
// 机制：
//   RX：数据面通过 PUNT_REASON_ARP 将 ARP 包推入 Punt RX 环；
//       固件主循环调用 hal_punt_rx_poll() 收包后调用 arp_process_pkt() 处理。
//   TX：arp_probe() 构造 ARP Request 并通过 hal_punt_tx_send() 发送；
//       数据面从 Punt TX 环取出后注入发送流水线。

#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
#define ARP_TABLE_SIZE      256     // 邻居表容量
#define ARP_AGE_MAX         300     // 老化时间（秒）
#define ARP_PROBE_RETRY_MAX 3       // ARP 请求最大重试次数
#define ARP_INCOMPLETE_TTL  5       // 未解析条目超时（秒）

// ARP 操作码
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

// 以太帧 EtherType
#define ETH_TYPE_ARP        0x0806
#define ETH_TYPE_IPV4       0x0800

// ─────────────────────────────────────────────
// 数据结构
// ─────────────────────────────────────────────

typedef enum {
    ARP_STATE_FREE       = 0,   // 空闲槽
    ARP_STATE_INCOMPLETE = 1,   // 已发送请求，等待回复
    ARP_STATE_REACHABLE  = 2,   // 完整可用条目
    ARP_STATE_STALE      = 3,   // 超过老化时间，需重新确认
} arp_state_t;

// 邻居表条目
typedef struct {
    uint32_t    ip;
    uint8_t     mac[6];
    port_id_t   port;
    uint16_t    vlan;
    uint32_t    age_ticks;      // 最后活跃时间（秒计数）
    uint8_t     retry;          // 剩余 probe 重试次数
    arp_state_t state;
} arp_entry_t;

// 本地 L3 接口信息（per port）
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint8_t  valid;
} l3_intf_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

/**
 * arp_init - 初始化 ARP 模块（清空表、安装 ARP Punt TCAM 规则）
 */
void arp_init(void);

/**
 * arp_set_port_intf - 配置端口的本地 L3 IP + MAC
 *   用于 ARP 应答时填写"发送方"字段
 */
void arp_set_port_intf(port_id_t port, uint32_t ip, const uint8_t *mac);

/**
 * arp_add - 手动添加/更新邻居表条目
 *   同时调用 fdb_learn() 更新 L2 转发表
 * 返回 HAL_OK 或 HAL_ERR_FULL
 */
int arp_add(uint32_t ip, const uint8_t *mac, port_id_t port, uint16_t vlan);

/**
 * arp_delete - 删除邻居条目（并从 FDB 移除对应记录）
 */
int arp_delete(uint32_t ip);

/**
 * arp_lookup - 查询下一跳 MAC 和出端口
 * 返回 HAL_OK（找到 REACHABLE 条目）或 -1（未找到/未完成）
 */
int arp_lookup(uint32_t ip, uint8_t *mac_out, port_id_t *port_out);

/**
 * arp_process_pkt - 处理从 Punt RX 环收到的 ARP 包
 *   自动识别 Request / Reply，并生成相应动作
 */
void arp_process_pkt(const punt_pkt_t *pkt);

/**
 * arp_probe - 向网络发送 ARP Request
 * @target_ip: 查询目标 IP
 * @eg_port:   出端口
 * @vlan:      所在 VLAN（0=无标签）
 * 返回 HAL_OK 或错误码
 */
int arp_probe(uint32_t target_ip, port_id_t eg_port, uint16_t vlan);

/**
 * arp_age - 周期性老化处理（每秒调用一次）
 * @now_sec: 当前时间（秒，单调递增）
 */
void arp_age(uint32_t now_sec);

/**
 * arp_show - 打印邻居表（调试）
 */
void arp_show(void);

#endif /* ARP_H */
