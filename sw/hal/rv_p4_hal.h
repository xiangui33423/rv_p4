// rv_p4_hal.h
// 硬件抽象层（HAL）— 控制面 C API
// 运行在香山核上，通过 MMIO 访问 TUE/CSR
// 由 RISC-V GCC/LLVM 编译

#ifndef RV_P4_HAL_H
#define RV_P4_HAL_H

#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────
// MMIO 基地址（与 ctrl_plane.sv 地址映射一致）
// ─────────────────────────────────────────────
#define HAL_BASE_PARSER     0xA0000000UL
#define HAL_BASE_MAU        0xA0001000UL
#define HAL_BASE_TM         0xA0002000UL
#define HAL_BASE_TUE        0xA0003000UL
#define HAL_BASE_PKTBUF     0xA0004000UL

// TUE 寄存器偏移（与 rv_p4_pkg.sv 一致）
#define TUE_REG_CMD         0x000
#define TUE_REG_TABLE_ID    0x004
#define TUE_REG_STAGE       0x008
#define TUE_REG_KEY_BASE    0x010   // key[0..15]，每个 32b，共 64B
#define TUE_REG_MASK_BASE   0x050   // mask[0..15]
#define TUE_REG_ACTION_ID   0x090
#define TUE_REG_ACTION_P0   0x094
#define TUE_REG_ACTION_P1   0x098
#define TUE_REG_ACTION_P2   0x09C
#define TUE_REG_STATUS      0x0A0
#define TUE_REG_COMMIT      0x0A4

// TUE 命令
#define TUE_CMD_INSERT      0x0
#define TUE_CMD_DELETE      0x1
#define TUE_CMD_MODIFY      0x2
#define TUE_CMD_FLUSH       0x3

// TUE 状态
#define TUE_STATUS_IDLE     0x0
#define TUE_STATUS_BUSY     0x1
#define TUE_STATUS_DONE     0x2
#define TUE_STATUS_ERROR    0x3

// ─────────────────────────────────────────────
// 类型定义
// ─────────────────────────────────────────────
typedef uint16_t table_id_t;
typedef uint16_t action_id_t;
typedef uint8_t  port_id_t;
typedef uint16_t counter_id_t;
typedef uint16_t meter_id_t;

// TCAM 匹配键（最大 512b = 64B）
typedef struct {
    uint8_t  bytes[64];
    uint8_t  key_len;   // 有效字节数
} tcam_key_t;

// TCAM 条目
typedef struct {
    tcam_key_t  key;
    tcam_key_t  mask;
    action_id_t action_id;
    uint8_t     action_params[12];  // 最多 12B 动作参数
    uint8_t     stage;              // 目标 MAU 级（0-23）
    uint16_t    table_id;
} tcam_entry_t;

// Meter 配置（srTCM）
typedef struct {
    uint32_t cir;   // 承诺信息速率（bps）
    uint32_t cbs;   // 承诺突发大小（bytes）
    uint32_t ebs;   // 超额突发大小（bytes）
} meter_cfg_t;

// 端口统计
typedef struct {
    uint64_t rx_pkts;
    uint64_t rx_bytes;
    uint64_t rx_drops;
    uint64_t tx_pkts;
    uint64_t tx_bytes;
    uint64_t tx_drops;
} port_stats_t;

// FSM 条目（Parser 状态转移）
typedef struct {
    uint8_t  cur_state;
    uint8_t  key_window[8];
    uint8_t  key_mask[8];
    uint8_t  next_state;
    uint8_t  extract_offset;
    uint8_t  extract_len;
    uint16_t phv_dst_offset;
    uint8_t  hdr_advance;
} fsm_entry_t;

// ─────────────────────────────────────────────
// 返回码
// ─────────────────────────────────────────────
#define HAL_OK          0
#define HAL_ERR_BUSY   -1
#define HAL_ERR_FULL   -2
#define HAL_ERR_INVAL  -3
#define HAL_ERR_TIMEOUT -4

// ─────────────────────────────────────────────
// MMIO 访问宏
// ─────────────────────────────────────────────
#define MMIO_WR32(addr, val) \
    (*(volatile uint32_t *)(uintptr_t)(addr) = (val))

#define MMIO_RD32(addr) \
    (*(volatile uint32_t *)(uintptr_t)(addr))

// ─────────────────────────────────────────────
// TCAM 表操作
// ─────────────────────────────────────────────

/**
 * hal_tcam_insert - 插入一条 TCAM 表项
 * @entry: 表项描述符（含 key/mask/action/stage/table_id）
 * 返回 HAL_OK 或错误码
 */
int hal_tcam_insert(const tcam_entry_t *entry);

/**
 * hal_tcam_delete - 删除一条 TCAM 表项
 * @stage:    目标 MAU 级
 * @table_id: 表 ID（对应 TCAM 条目索引）
 */
int hal_tcam_delete(uint8_t stage, uint16_t table_id);

/**
 * hal_tcam_modify - 修改已有表项的 action
 */
int hal_tcam_modify(const tcam_entry_t *entry);

/**
 * hal_tcam_flush - 清空某级 MAU 的所有表项
 */
int hal_tcam_flush(uint8_t stage);

// ─────────────────────────────────────────────
// 计数器操作
// ─────────────────────────────────────────────
int hal_counter_read(counter_id_t id, uint64_t *bytes, uint64_t *pkts);
int hal_counter_reset(counter_id_t id);

// ─────────────────────────────────────────────
// Meter 操作
// ─────────────────────────────────────────────
int hal_meter_config(meter_id_t id, const meter_cfg_t *cfg);

// ─────────────────────────────────────────────
// Parser FSM 动态更新
// ─────────────────────────────────────────────
int hal_parser_add_state(const fsm_entry_t *entry);
int hal_parser_del_state(uint8_t state_id);

// ─────────────────────────────────────────────
// 端口管理
// ─────────────────────────────────────────────
int hal_port_enable(port_id_t port);
int hal_port_disable(port_id_t port);
int hal_port_stats(port_id_t port, port_stats_t *stats);
int hal_port_stats_clear(port_id_t port);

// ─────────────────────────────────────────────
// 初始化
// ─────────────────────────────────────────────
int hal_init(void);

// ─────────────────────────────────────────────
// VLAN 配置（通过 HAL_BASE_VLAN CSR）
// ─────────────────────────────────────────────
#define HAL_BASE_VLAN       0xA0005000UL

/* 寄存器偏移 */
#define VLAN_REG_PORT_PVID(p)     (0x000 + (unsigned)(p)*4)    // [11:0] PVID
#define VLAN_REG_PORT_MODE(p)     (0x100 + (unsigned)(p)*4)    // 0=access,1=trunk
#define VLAN_REG_MEMBER(v)        (0x200 + (unsigned)(v)*4)    // bit[p]=端口 p 为成员
#define VLAN_REG_UNTAGGED(v)      (0x600 + (unsigned)(v)*4)    // bit[p]=端口 p 无标签出

/* 端口模式 */
#define VLAN_MODE_ACCESS    0
#define VLAN_MODE_TRUNK     1

int hal_vlan_pvid_set(port_id_t port, uint16_t vlan_id);
int hal_vlan_mode_set(port_id_t port, uint8_t mode);
int hal_vlan_member_set(uint16_t vlan_id, uint32_t member_bitmap,
                        uint32_t untagged_bitmap);
uint32_t hal_vlan_member_get(uint16_t vlan_id);

// ─────────────────────────────────────────────
// QoS 配置（通过 HAL_BASE_QOS CSR）
// ─────────────────────────────────────────────
#define HAL_BASE_QOS        0xA0006000UL

/* 寄存器偏移：port=0..31, queue=0..7 */
#define QOS_REG_DWRR(p,q)         (0x000 + (unsigned)(p)*0x20 + (unsigned)(q)*4)
#define QOS_REG_PIR(p)            (0x400 + (unsigned)(p)*4)    // 峰值速率 bps
#define QOS_REG_SCHED_MODE(p)     (0x480 + (unsigned)(p)*4)    // 调度模式
#define QOS_REG_DSCP_MAP(d)       (0x500 + (unsigned)(d)*4)    // DSCP[5:0]→队列[2:0]

/* 调度模式枚举 */
#define QOS_SCHED_DWRR      0    // 纯 DWRR
#define QOS_SCHED_SP        1    // 纯 Strict Priority
#define QOS_SCHED_SP_DWRR   2    // 高优先级 SP + 低优先级 DWRR

int hal_qos_dwrr_set(port_id_t port, uint8_t queue, uint32_t weight_bytes);
int hal_qos_pir_set(port_id_t port, uint64_t bps);
int hal_qos_sched_mode_set(port_id_t port, uint8_t mode);
int hal_qos_dscp_map_set(uint8_t dscp, uint8_t queue);

// ─────────────────────────────────────────────
// Punt-to-CPU 机制（通过 HAL_BASE_PUNT 共享环）
// ─────────────────────────────────────────────
#define HAL_BASE_PUNT       0xA0007000UL

#define PUNT_REG_RX_PROD    0x000   // HW 写：下一个可读槽
#define PUNT_REG_RX_CONS    0x004   // CPU 写：已消费指针
#define PUNT_REG_TX_PROD    0x008   // CPU 写：下一个填写槽
#define PUNT_REG_TX_CONS    0x00C   // HW 读：已消费指针
#define PUNT_REG_STATUS     0x010   // bit[0]=rx_avail, bit[1]=tx_full

#define PUNT_RING_RX_BASE   0x100   // RX ring 起始
#define PUNT_RING_TX_BASE   0x1500  // TX ring 起始
#define PUNT_RING_SLOTS     16
#define PUNT_SLOT_SIZE      320     // 8B 描述符 + 256B 数据 + 56B 填充

/* Punt 包描述符（对应 PUNT_SLOT 首 8 字节） */
typedef struct {
    uint8_t  ing_port;     /* 入端口（RX 有效） */
    uint8_t  eg_port;      /* 出端口（TX 有效） */
    uint16_t pkt_len;      /* 有效数据字节数 */
    uint16_t vlan_id;      /* VLAN ID */
    uint8_t  reason;       /* punt 原因：0=ARP,1=Other */
    uint8_t  _pad;
    uint8_t  data[256];    /* 包数据（含以太网头） */
} punt_pkt_t;

/* punt reason 值 */
#define PUNT_REASON_ARP     0
#define PUNT_REASON_OTHER   1

int hal_punt_rx_poll(punt_pkt_t *pkt);      /* 有包返回 HAL_OK，否则 -1 */
int hal_punt_tx_send(const punt_pkt_t *pkt); /* 写到 TX ring */

// ─────────────────────────────────────────────
// UART（控制台 I/O，用于 CLI）
// ─────────────────────────────────────────────
#define HAL_BASE_UART       0xA0009000UL

#define UART_REG_DATA       0x000   // [7:0] RX/TX 数据字节
#define UART_REG_STATUS     0x004   // bit[0]=rx_avail, bit[1]=tx_ready
#define UART_STATUS_RX_AVAIL  (1U << 0)
#define UART_STATUS_TX_READY  (1U << 1)

/**
 * hal_uart_putc - 向 UART 输出单个字符（阻塞等待 TX ready）
 */
int  hal_uart_putc(char c);

/**
 * hal_uart_getc - 读取一个 RX 字符，无字符时返回 -1（非阻塞）
 */
int  hal_uart_getc(void);

/**
 * hal_uart_puts - 输出 NUL 结尾的字符串
 */
void hal_uart_puts(const char *s);

#endif /* RV_P4_HAL_H */
