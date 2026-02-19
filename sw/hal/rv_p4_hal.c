// rv_p4_hal.c
// HAL 实现 — MMIO 驱动 TUE/CSR
// 编译：riscv64-unknown-linux-gnu-gcc -O2 -march=rv64gc rv_p4_hal.c

#include "rv_p4_hal.h"

// ─────────────────────────────────────────────
// 内部工具函数
// ─────────────────────────────────────────────

// 等待 TUE 空闲（轮询 STATUS 寄存器）
static int tue_wait_idle(void) {
    int timeout = 100000;
    while (timeout--) {
        uint32_t st = MMIO_RD32(HAL_BASE_TUE + TUE_REG_STATUS);
        if ((st & 0x3) == TUE_STATUS_IDLE ||
            (st & 0x3) == TUE_STATUS_DONE)
            return HAL_OK;
        if ((st & 0x3) == TUE_STATUS_ERROR)
            return HAL_ERR_BUSY;
    }
    return HAL_ERR_TIMEOUT;
}

// 将 64B key/mask 写入 TUE 寄存器（16 × 32b）
static void tue_write_key(uint32_t base_off, const uint8_t *data, uint8_t len) {
    uint8_t buf[64] = {0};
    uint8_t n = (len > 64) ? 64 : len;
    for (int i = 0; i < n; i++) buf[i] = data[i];
    for (int i = 0; i < 16; i++) {
        uint32_t word = ((uint32_t)buf[i*4+0])       |
                        ((uint32_t)buf[i*4+1] << 8)  |
                        ((uint32_t)buf[i*4+2] << 16) |
                        ((uint32_t)buf[i*4+3] << 24);
        MMIO_WR32(HAL_BASE_TUE + base_off + i * 4, word);
    }
}

// 提交 TUE 事务并等待完成
static int tue_commit(void) {
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_COMMIT, 0x1);
    return tue_wait_idle();
}

// ─────────────────────────────────────────────
// TCAM 操作实现
// ─────────────────────────────────────────────

int hal_tcam_insert(const tcam_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;

    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    // 写命令和目标
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,      TUE_CMD_INSERT);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_TABLE_ID, entry->table_id);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE,    entry->stage);

    // 写 key / mask
    tue_write_key(TUE_REG_KEY_BASE,  entry->key.bytes,  entry->key.key_len);
    tue_write_key(TUE_REG_MASK_BASE, entry->mask.bytes, entry->mask.key_len);

    // 写 action
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_ID, entry->action_id);

    uint32_t p0 = 0, p1 = 0, p2 = 0;
    for (int i = 0; i < 4 && i < 12; i++)
        p0 |= ((uint32_t)entry->action_params[i] << (i * 8));
    for (int i = 0; i < 4 && (i+4) < 12; i++)
        p1 |= ((uint32_t)entry->action_params[i+4] << (i * 8));
    for (int i = 0; i < 4 && (i+8) < 12; i++)
        p2 |= ((uint32_t)entry->action_params[i+8] << (i * 8));

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P0, p0);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P1, p1);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P2, p2);

    return tue_commit();
}

int hal_tcam_delete(uint8_t stage, uint16_t table_id) {
    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,      TUE_CMD_DELETE);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_TABLE_ID, table_id);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE,    stage);

    return tue_commit();
}

int hal_tcam_modify(const tcam_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;

    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,      TUE_CMD_MODIFY);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_TABLE_ID, entry->table_id);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE,    entry->stage);

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_ID, entry->action_id);

    uint32_t p0 = 0, p1 = 0, p2 = 0;
    for (int i = 0; i < 4; i++) p0 |= ((uint32_t)entry->action_params[i]   << (i*8));
    for (int i = 0; i < 4; i++) p1 |= ((uint32_t)entry->action_params[i+4] << (i*8));
    for (int i = 0; i < 4; i++) p2 |= ((uint32_t)entry->action_params[i+8] << (i*8));

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P0, p0);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P1, p1);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_ACTION_P2, p2);

    return tue_commit();
}

int hal_tcam_flush(uint8_t stage) {
    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,   TUE_CMD_FLUSH);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE, stage);

    return tue_commit();
}

// ─────────────────────────────────────────────
// 计数器（Stateful SRAM，通过 MAU CSR 读取）
// ─────────────────────────────────────────────
int hal_counter_read(counter_id_t id, uint64_t *bytes, uint64_t *pkts) {
    if (!bytes || !pkts) return HAL_ERR_INVAL;
    // 计数器地址：MAU CSR 基地址 + 计数器偏移
    uint32_t off = 0x100 + id * 8;
    uint32_t lo  = MMIO_RD32(HAL_BASE_MAU + off);
    uint32_t hi  = MMIO_RD32(HAL_BASE_MAU + off + 4);
    *bytes = ((uint64_t)hi << 32) | lo;
    *pkts  = 0; // 简化：仅字节计数器
    return HAL_OK;
}

int hal_counter_reset(counter_id_t id) {
    uint32_t off = 0x100 + id * 8;
    MMIO_WR32(HAL_BASE_MAU + off,     0);
    MMIO_WR32(HAL_BASE_MAU + off + 4, 0);
    return HAL_OK;
}

// ─────────────────────────────────────────────
// Meter（通过 TM CSR 配置）
// ─────────────────────────────────────────────
int hal_meter_config(meter_id_t id, const meter_cfg_t *cfg) {
    if (!cfg) return HAL_ERR_INVAL;
    uint32_t off = 0x200 + id * 12;
    MMIO_WR32(HAL_BASE_TM + off,     cfg->cir);
    MMIO_WR32(HAL_BASE_TM + off + 4, cfg->cbs);
    MMIO_WR32(HAL_BASE_TM + off + 8, cfg->ebs);
    return HAL_OK;
}

// ─────────────────────────────────────────────
// Parser FSM 更新（通过 TUE stage=0x1F）
// ─────────────────────────────────────────────
int hal_parser_add_state(const fsm_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;

    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    // 将 FSM 条目编码为 key 字段传递给 TUE
    // stage=0x1F 表示 Parser 目标
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,      TUE_CMD_INSERT);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE,    0x1F);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_TABLE_ID, entry->cur_state);

    // key[0] = cur_state + key_window[0..7]
    uint8_t key_buf[64] = {0};
    key_buf[0] = entry->cur_state;
    for (int i = 0; i < 8; i++) key_buf[1+i] = entry->key_window[i];
    for (int i = 0; i < 8; i++) key_buf[9+i] = entry->key_mask[i];
    key_buf[17] = entry->next_state;
    key_buf[18] = entry->extract_offset;
    key_buf[19] = entry->extract_len;
    key_buf[20] = (entry->phv_dst_offset >> 8) & 0x3;
    key_buf[21] = entry->phv_dst_offset & 0xFF;
    key_buf[22] = entry->hdr_advance;

    tue_write_key(TUE_REG_KEY_BASE, key_buf, 23);

    return tue_commit();
}

int hal_parser_del_state(uint8_t state_id) {
    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    MMIO_WR32(HAL_BASE_TUE + TUE_REG_CMD,      TUE_CMD_DELETE);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_STAGE,    0x1F);
    MMIO_WR32(HAL_BASE_TUE + TUE_REG_TABLE_ID, state_id);

    return tue_commit();
}

// ─────────────────────────────────────────────
// 端口管理（通过 Parser/TM CSR）
// ─────────────────────────────────────────────
int hal_port_enable(port_id_t port) {
    if (port >= 32) return HAL_ERR_INVAL;
    uint32_t reg = MMIO_RD32(HAL_BASE_PARSER + 0x000);
    reg |= (1U << port);
    MMIO_WR32(HAL_BASE_PARSER + 0x000, reg);
    return HAL_OK;
}

int hal_port_disable(port_id_t port) {
    if (port >= 32) return HAL_ERR_INVAL;
    uint32_t reg = MMIO_RD32(HAL_BASE_PARSER + 0x000);
    reg &= ~(1U << port);
    MMIO_WR32(HAL_BASE_PARSER + 0x000, reg);
    return HAL_OK;
}

int hal_port_stats(port_id_t port, port_stats_t *stats) {
    if (port >= 32 || !stats) return HAL_ERR_INVAL;
    uint32_t off = 0x400 + port * 0x20;
    uint32_t lo, hi;

    lo = MMIO_RD32(HAL_BASE_TM + off + 0x00);
    hi = MMIO_RD32(HAL_BASE_TM + off + 0x04);
    stats->rx_pkts = ((uint64_t)hi << 32) | lo;

    lo = MMIO_RD32(HAL_BASE_TM + off + 0x08);
    hi = MMIO_RD32(HAL_BASE_TM + off + 0x0C);
    stats->rx_bytes = ((uint64_t)hi << 32) | lo;

    lo = MMIO_RD32(HAL_BASE_TM + off + 0x10);
    hi = MMIO_RD32(HAL_BASE_TM + off + 0x14);
    stats->tx_pkts = ((uint64_t)hi << 32) | lo;

    lo = MMIO_RD32(HAL_BASE_TM + off + 0x18);
    hi = MMIO_RD32(HAL_BASE_TM + off + 0x1C);
    stats->tx_bytes = ((uint64_t)hi << 32) | lo;

    stats->rx_drops = 0;
    stats->tx_drops = 0;
    return HAL_OK;
}

int hal_port_stats_clear(port_id_t port) {
    if (port >= 32) return HAL_ERR_INVAL;
    uint32_t off = 0x400 + port * 0x20;
    for (int i = 0; i < 8; i++)
        MMIO_WR32(HAL_BASE_TM + off + i * 4, 0);
    return HAL_OK;
}

// ─────────────────────────────────────────────
// VLAN CSR
// ─────────────────────────────────────────────

int hal_vlan_pvid_set(port_id_t port, uint16_t vlan_id) {
    if (port >= 32 || vlan_id > 4095) return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_VLAN + VLAN_REG_PORT_PVID(port), vlan_id);
    return HAL_OK;
}

int hal_vlan_mode_set(port_id_t port, uint8_t mode) {
    if (port >= 32 || (mode != VLAN_MODE_ACCESS && mode != VLAN_MODE_TRUNK))
        return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_VLAN + VLAN_REG_PORT_MODE(port), mode);
    return HAL_OK;
}

int hal_vlan_member_set(uint16_t vlan_id, uint32_t member_bitmap,
                        uint32_t untagged_bitmap) {
    if (vlan_id > 255) return HAL_ERR_INVAL;   // 当前支持 0-255
    MMIO_WR32(HAL_BASE_VLAN + VLAN_REG_MEMBER(vlan_id),   member_bitmap);
    MMIO_WR32(HAL_BASE_VLAN + VLAN_REG_UNTAGGED(vlan_id), untagged_bitmap);
    return HAL_OK;
}

uint32_t hal_vlan_member_get(uint16_t vlan_id) {
    if (vlan_id > 255) return 0;
    return MMIO_RD32(HAL_BASE_VLAN + VLAN_REG_MEMBER(vlan_id));
}

// ─────────────────────────────────────────────
// QoS CSR
// ─────────────────────────────────────────────

int hal_qos_dwrr_set(port_id_t port, uint8_t queue, uint32_t weight_bytes) {
    if (port >= 32 || queue >= 8) return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_QOS + QOS_REG_DWRR(port, queue), weight_bytes);
    return HAL_OK;
}

int hal_qos_pir_set(port_id_t port, uint64_t bps) {
    if (port >= 32) return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_QOS + QOS_REG_PIR(port), (uint32_t)(bps & 0xFFFFFFFF));
    MMIO_WR32(HAL_BASE_QOS + QOS_REG_PIR(port) + 4, (uint32_t)(bps >> 32));
    return HAL_OK;
}

int hal_qos_sched_mode_set(port_id_t port, uint8_t mode) {
    if (port >= 32 || mode > QOS_SCHED_SP_DWRR) return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_QOS + QOS_REG_SCHED_MODE(port), mode);
    return HAL_OK;
}

int hal_qos_dscp_map_set(uint8_t dscp, uint8_t queue) {
    if (dscp >= 64 || queue >= 8) return HAL_ERR_INVAL;
    MMIO_WR32(HAL_BASE_QOS + QOS_REG_DSCP_MAP(dscp), queue);
    return HAL_OK;
}

// ─────────────────────────────────────────────
// Punt-to-CPU 环形缓冲
// ─────────────────────────────────────────────

/*
 * RX ring（HW→CPU）：
 *   slot 起始地址 = PUNT_RING_RX_BASE + (prod % SLOTS) * SLOT_SIZE
 *   描述符 = punt_pkt_t，前 8 字节为 ing_port/eg_port/pkt_len/vlan_id/reason
 */
int hal_punt_rx_poll(punt_pkt_t *pkt) {
    if (!pkt) return HAL_ERR_INVAL;

    uint32_t prod = MMIO_RD32(HAL_BASE_PUNT + PUNT_REG_RX_PROD);
    uint32_t cons = MMIO_RD32(HAL_BASE_PUNT + PUNT_REG_RX_CONS);
    if (prod == cons)
        return -1;   // 环空

    /* 计算槽地址 */
    uint32_t slot = cons % PUNT_RING_SLOTS;
    uintptr_t base = (uintptr_t)(HAL_BASE_PUNT + PUNT_RING_RX_BASE +
                                 slot * PUNT_SLOT_SIZE);
    volatile uint32_t *p = (volatile uint32_t *)base;

    /* 读描述符（前 2 个字 = 8B） */
    uint32_t w0 = p[0];
    uint32_t w1 = p[1];
    pkt->ing_port = (uint8_t)(w0 & 0xFF);
    pkt->eg_port  = (uint8_t)((w0 >> 8) & 0xFF);
    pkt->pkt_len  = (uint16_t)((w0 >> 16) & 0xFFFF);
    pkt->vlan_id  = (uint16_t)(w1 & 0xFFFF);
    pkt->reason   = (uint8_t)((w1 >> 16) & 0xFF);

    /* 读包数据（最多 256B，按 32b 读取） */
    uint16_t data_len = pkt->pkt_len;
    if (data_len > 256) data_len = 256;
    uint16_t words = (data_len + 3) / 4;
    for (uint16_t i = 0; i < words; i++) {
        uint32_t d = p[2 + i];
        uint16_t off = (uint16_t)(i * 4);
        pkt->data[off]   = (uint8_t)(d & 0xFF);
        if (off + 1 < data_len) pkt->data[off+1] = (uint8_t)((d >> 8)  & 0xFF);
        if (off + 2 < data_len) pkt->data[off+2] = (uint8_t)((d >> 16) & 0xFF);
        if (off + 3 < data_len) pkt->data[off+3] = (uint8_t)((d >> 24) & 0xFF);
    }

    /* 消费指针推进 */
    MMIO_WR32(HAL_BASE_PUNT + PUNT_REG_RX_CONS, cons + 1);
    return HAL_OK;
}

int hal_punt_tx_send(const punt_pkt_t *pkt) {
    if (!pkt) return HAL_ERR_INVAL;

    uint32_t prod = MMIO_RD32(HAL_BASE_PUNT + PUNT_REG_TX_PROD);
    uint32_t cons = MMIO_RD32(HAL_BASE_PUNT + PUNT_REG_TX_CONS);
    if ((prod - cons) >= PUNT_RING_SLOTS)
        return HAL_ERR_FULL;   // TX 环满

    uint32_t slot = prod % PUNT_RING_SLOTS;
    uintptr_t base = (uintptr_t)(HAL_BASE_PUNT + PUNT_RING_TX_BASE +
                                 slot * PUNT_SLOT_SIZE);
    volatile uint32_t *p = (volatile uint32_t *)base;

    uint16_t data_len = pkt->pkt_len;
    if (data_len > 256) data_len = 256;

    /* 写描述符 */
    p[0] = (uint32_t)pkt->ing_port        |
           ((uint32_t)pkt->eg_port  << 8)  |
           ((uint32_t)data_len      << 16);
    p[1] = (uint32_t)pkt->vlan_id         |
           ((uint32_t)pkt->reason   << 16);

    /* 写包数据 */
    uint16_t words = (data_len + 3) / 4;
    for (uint16_t i = 0; i < words; i++) {
        uint16_t off = (uint16_t)(i * 4);
        uint32_t d = (uint32_t)pkt->data[off];
        if (off + 1 < data_len) d |= (uint32_t)pkt->data[off+1] << 8;
        if (off + 2 < data_len) d |= (uint32_t)pkt->data[off+2] << 16;
        if (off + 3 < data_len) d |= (uint32_t)pkt->data[off+3] << 24;
        p[2 + i] = d;
    }

    /* 生产指针推进（HW 看到变化后发送） */
    MMIO_WR32(HAL_BASE_PUNT + PUNT_REG_TX_PROD, prod + 1);
    return HAL_OK;
}

// ─────────────────────────────────────────────
// UART（控制台 I/O）
// ─────────────────────────────────────────────

int hal_uart_putc(char c) {
    /* 等待 TX ready */
    int timeout = 100000;
    while (timeout--) {
        if (MMIO_RD32(HAL_BASE_UART + UART_REG_STATUS) & UART_STATUS_TX_READY)
            break;
    }
    MMIO_WR32(HAL_BASE_UART + UART_REG_DATA, (uint32_t)(uint8_t)c);
    return HAL_OK;
}

int hal_uart_getc(void) {
    if (!(MMIO_RD32(HAL_BASE_UART + UART_REG_STATUS) & UART_STATUS_RX_AVAIL))
        return -1;
    return (int)(MMIO_RD32(HAL_BASE_UART + UART_REG_DATA) & 0xFF);
}

void hal_uart_puts(const char *s) {
    while (*s) hal_uart_putc(*s++);
}

// ─────────────────────────────────────────────
// 初始化
// ─────────────────────────────────────────────
int hal_init(void) {
    // 等待 TUE 就绪
    int ret = tue_wait_idle();
    if (ret != HAL_OK) return ret;

    // 使能所有端口
    MMIO_WR32(HAL_BASE_PARSER + 0x000, 0xFFFFFFFF);

    return HAL_OK;
}
