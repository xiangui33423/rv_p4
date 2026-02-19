// sim_hal.c
// 模拟 HAL 实现
// 提供所有 rv_p4_hal.h 声明的函数（TCAM/VLAN/QoS/Punt/UART）

#include "sim_hal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// 全局模拟状态定义
// ─────────────────────────────────────────────

sim_tcam_rec_t sim_tcam_db[SIM_TCAM_MAX];
int            sim_tcam_n;

uint16_t  sim_vlan_pvid[32];
uint8_t   sim_vlan_mode[32];
uint32_t  sim_vlan_member[256];
uint32_t  sim_vlan_untagged[256];

uint32_t  sim_qos_dwrr[32][8];
uint64_t  sim_qos_pir[32];
uint8_t   sim_qos_mode[32];
uint8_t   sim_qos_dscp_map[64];

uint32_t  sim_port_enable;

sim_punt_rec_t sim_punt_rx_ring[SIM_PUNT_MAX];
int            sim_punt_rx_head;
int            sim_punt_rx_tail;

sim_punt_rec_t sim_punt_tx_ring[SIM_PUNT_MAX];
int            sim_punt_tx_head;
int            sim_punt_tx_tail;

// ─────────────────────────────────────────────
// sim_hal_reset
// ─────────────────────────────────────────────

void sim_hal_reset(void) {
    memset(sim_tcam_db,     0, sizeof(sim_tcam_db));
    sim_tcam_n = 0;

    memset(sim_vlan_pvid,   0, sizeof(sim_vlan_pvid));
    memset(sim_vlan_mode,   0, sizeof(sim_vlan_mode));
    memset(sim_vlan_member, 0, sizeof(sim_vlan_member));
    memset(sim_vlan_untagged,0,sizeof(sim_vlan_untagged));

    memset(sim_qos_dwrr,    0, sizeof(sim_qos_dwrr));
    memset(sim_qos_pir,     0, sizeof(sim_qos_pir));
    memset(sim_qos_mode,    0, sizeof(sim_qos_mode));
    memset(sim_qos_dscp_map,0, sizeof(sim_qos_dscp_map));

    sim_port_enable = 0;

    memset(sim_punt_rx_ring, 0, sizeof(sim_punt_rx_ring));
    memset(sim_punt_tx_ring, 0, sizeof(sim_punt_tx_ring));
    sim_punt_rx_head = sim_punt_rx_tail = 0;
    sim_punt_tx_head = sim_punt_tx_tail = 0;
}

// ─────────────────────────────────────────────
// TCAM 辅助
// ─────────────────────────────────────────────

sim_tcam_rec_t *sim_tcam_find(uint8_t stage, uint16_t table_id) {
    for (int i = 0; i < sim_tcam_n; i++) {
        sim_tcam_rec_t *r = &sim_tcam_db[i];
        if (r->valid && !r->deleted &&
            r->entry.stage    == stage &&
            r->entry.table_id == table_id)
            return r;
    }
    return NULL;
}

int sim_tcam_count_stage(uint8_t stage) {
    int cnt = 0;
    for (int i = 0; i < sim_tcam_n; i++) {
        sim_tcam_rec_t *r = &sim_tcam_db[i];
        if (r->valid && !r->deleted && r->entry.stage == stage)
            cnt++;
    }
    return cnt;
}

// ─────────────────────────────────────────────
// HAL: TCAM 操作
// ─────────────────────────────────────────────

int hal_tcam_insert(const tcam_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;

    // 已存在则更新
    sim_tcam_rec_t *ex = sim_tcam_find(entry->stage, entry->table_id);
    if (ex) {
        ex->entry   = *entry;
        ex->deleted = 0;
        return HAL_OK;
    }
    // 新条目
    if (sim_tcam_n >= SIM_TCAM_MAX) return HAL_ERR_FULL;
    sim_tcam_db[sim_tcam_n].entry   = *entry;
    sim_tcam_db[sim_tcam_n].valid   = 1;
    sim_tcam_db[sim_tcam_n].deleted = 0;
    sim_tcam_n++;
    return HAL_OK;
}

int hal_tcam_delete(uint8_t stage, uint16_t table_id) {
    sim_tcam_rec_t *e = sim_tcam_find(stage, table_id);
    if (!e) return HAL_ERR_INVAL;
    e->deleted = 1;
    return HAL_OK;
}

int hal_tcam_modify(const tcam_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;
    sim_tcam_rec_t *e = sim_tcam_find(entry->stage, entry->table_id);
    if (!e) return HAL_ERR_INVAL;
    e->entry = *entry;
    return HAL_OK;
}

int hal_tcam_flush(uint8_t stage) {
    for (int i = 0; i < sim_tcam_n; i++)
        if (sim_tcam_db[i].valid && sim_tcam_db[i].entry.stage == stage)
            sim_tcam_db[i].deleted = 1;
    return HAL_OK;
}

// ─────────────────────────────────────────────
// HAL: VLAN CSR
// ─────────────────────────────────────────────

int hal_vlan_pvid_set(port_id_t port, uint16_t vlan_id) {
    if (port >= 32 || vlan_id > 4095) return HAL_ERR_INVAL;
    sim_vlan_pvid[port] = vlan_id;
    return HAL_OK;
}

int hal_vlan_mode_set(port_id_t port, uint8_t mode) {
    if (port >= 32) return HAL_ERR_INVAL;
    sim_vlan_mode[port] = mode;
    return HAL_OK;
}

int hal_vlan_member_set(uint16_t vlan_id, uint32_t member, uint32_t untagged) {
    if (vlan_id > 255) return HAL_ERR_INVAL;
    sim_vlan_member[vlan_id]   = member;
    sim_vlan_untagged[vlan_id] = untagged;
    return HAL_OK;
}

uint32_t hal_vlan_member_get(uint16_t vlan_id) {
    if (vlan_id > 255) return 0;
    return sim_vlan_member[vlan_id];
}

// ─────────────────────────────────────────────
// HAL: QoS CSR
// ─────────────────────────────────────────────

int hal_qos_dwrr_set(port_id_t port, uint8_t queue, uint32_t weight) {
    if (port >= 32 || queue >= 8) return HAL_ERR_INVAL;
    sim_qos_dwrr[port][queue] = weight;
    return HAL_OK;
}

int hal_qos_pir_set(port_id_t port, uint64_t bps) {
    if (port >= 32) return HAL_ERR_INVAL;
    sim_qos_pir[port] = bps;
    return HAL_OK;
}

int hal_qos_sched_mode_set(port_id_t port, uint8_t mode) {
    if (port >= 32 || mode > QOS_SCHED_SP_DWRR) return HAL_ERR_INVAL;
    sim_qos_mode[port] = mode;
    return HAL_OK;
}

int hal_qos_dscp_map_set(uint8_t dscp, uint8_t queue) {
    if (dscp >= 64 || queue >= 8) return HAL_ERR_INVAL;
    sim_qos_dscp_map[dscp] = queue;
    return HAL_OK;
}

// ─────────────────────────────────────────────
// HAL: 端口管理
// ─────────────────────────────────────────────

int hal_port_enable(port_id_t port) {
    if (port >= 32) return HAL_ERR_INVAL;
    sim_port_enable |=  (1U << port);
    return HAL_OK;
}

int hal_port_disable(port_id_t port) {
    if (port >= 32) return HAL_ERR_INVAL;
    sim_port_enable &= ~(1U << port);
    return HAL_OK;
}

int hal_port_stats(port_id_t port, port_stats_t *s) {
    if (port >= 32 || !s) return HAL_ERR_INVAL;
    memset(s, 0, sizeof(*s));
    return HAL_OK;
}

int hal_port_stats_clear(port_id_t port) {
    (void)port;
    return HAL_OK;
}

// ─────────────────────────────────────────────
// HAL: Punt 环
// ─────────────────────────────────────────────

void sim_punt_rx_inject(const punt_pkt_t *pkt) {
    int slot = sim_punt_rx_head % SIM_PUNT_MAX;
    sim_punt_rx_ring[slot].pkt   = *pkt;
    sim_punt_rx_ring[slot].valid = 1;
    sim_punt_rx_head++;
}

int hal_punt_rx_poll(punt_pkt_t *pkt) {
    if (sim_punt_rx_tail >= sim_punt_rx_head) return -1;
    int slot = sim_punt_rx_tail % SIM_PUNT_MAX;
    if (!sim_punt_rx_ring[slot].valid)        return -1;
    *pkt = sim_punt_rx_ring[slot].pkt;
    sim_punt_rx_ring[slot].valid = 0;
    sim_punt_rx_tail++;
    return HAL_OK;
}

int hal_punt_tx_send(const punt_pkt_t *pkt) {
    if (!pkt) return HAL_ERR_INVAL;
    if (sim_punt_tx_head - sim_punt_tx_tail >= SIM_PUNT_MAX)
        return HAL_ERR_FULL;
    int slot = sim_punt_tx_head % SIM_PUNT_MAX;
    sim_punt_tx_ring[slot].pkt   = *pkt;
    sim_punt_tx_ring[slot].valid = 1;
    sim_punt_tx_head++;
    return HAL_OK;
}

// ─────────────────────────────────────────────
// HAL: 其余 stub
// ─────────────────────────────────────────────

int hal_counter_read(counter_id_t id, uint64_t *b, uint64_t *p) {
    (void)id;
    if (b) *b = 0;
    if (p) *p = 0;
    return HAL_OK;
}

int hal_counter_reset(counter_id_t id)            { (void)id;         return HAL_OK; }
int hal_meter_config(meter_id_t id, const meter_cfg_t *c) { (void)id; (void)c; return HAL_OK; }
int hal_parser_add_state(const fsm_entry_t *e)    { (void)e;          return HAL_OK; }
int hal_parser_del_state(uint8_t s)               { (void)s;          return HAL_OK; }

int hal_init(void) {
    sim_hal_reset();
    return HAL_OK;
}

// ─────────────────────────────────────────────
// UART stub（用于 CLI 测试，输出到 stdout）
// ─────────────────────────────────────────────

int hal_uart_putc(char c) {
    printf("%c", c);
    return HAL_OK;
}

int hal_uart_getc(void) {
    return -1;   /* 测试环境无 UART 输入 */
}

void hal_uart_puts(const char *s) {
    printf("%s", s);
}
