// cosim_main.cpp
// RTL Co-Simulation: Control Plane Firmware + Verilator Data Plane
//
// Architecture:
//   1. Control plane firmware C code (route_add, fdb_add_static) calls
//      HAL functions (hal_tcam_insert, etc.) implemented here.
//   2. HAL converts firmware TCAM entries to RTL-native format and
//      programs the TUE via APB through the tb_tue_* backdoor ports.
//   3. Parser TCAM is loaded via tb_parser_wr_* backdoor to extract
//      the relevant packet fields into PHV positions that match the
//      firmware's TCAM key encoding.
//   4. Packets are injected via MAC RX signals and TX is monitored
//      to verify forwarding decisions.
//
// Key conventions:
//   Firmware mask: bit=1 → must match,  bit=0 → don't care
//   RTL mask:      bit=1 → don't care,  bit=0 → must match
//   Conversion: rtl_mask = ~fw_mask
//
// Tests:
//   CS-RTL-1: IPv4 LPM routing  → packet exits on expected TX port
//   CS-RTL-2: L2 FDB forwarding → packet exits on expected TX port
//   CS-RTL-3: ACL Deny          → no TX output (packet dropped)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include <verilated.h>
#include "Vrv_p4_top.h"

#include "../../sw/hal/rv_p4_hal.h"
#include "../../sw/firmware/table_map.h"
#include "../../sw/firmware/route.h"
#include "../../sw/firmware/fdb.h"
#include "../../sw/firmware/acl.h"

// ─────────────────────────────────────────────────────────────────────────────
// Global simulation state
// ─────────────────────────────────────────────────────────────────────────────

static Vrv_p4_top *g_top = nullptr;
static uint64_t    g_sim_time = 0;

// Test counters
static int g_pass = 0;
static int g_fail = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Clock management
//
// We simulate at half-period resolution of clk_dp.
// Ratios (half-periods of clk_dp):
//   clk_dp   : 1 per tick  (1.6 GHz)
//   clk_ctrl : 8 per tick  (200 MHz,  ratio dp:ctrl = 8:1)
//   clk_mac  : 4 per tick  (390.625 MHz, ratio dp:mac ≈ 4:1)
//   clk_cpu  : 1 per tick  (same phase as clk_dp for simplicity)
// ─────────────────────────────────────────────────────────────────────────────

static void step_half() {
    g_sim_time++;
    g_top->clk_dp   = (g_sim_time       % 2 == 0) ? 1 : 0;
    g_top->clk_cpu  = (g_sim_time       % 2 == 0) ? 1 : 0;
    g_top->clk_ctrl = ((g_sim_time / 8) % 2 == 0) ? 1 : 0;
    g_top->clk_mac  = ((g_sim_time / 4) % 2 == 0) ? 1 : 0;
    g_top->eval();
}

// Step n dp rising+falling edges (n full dp cycles)
static void step_dp(int n) {
    for (int i = 0; i < n * 2; i++) step_half();
}

// Step n ctrl cycles (each ctrl cycle = 8 dp cycles)
static void step_ctrl(int n) {
    step_dp(n * 8);
}

// Step n mac cycles (each mac cycle ≈ 4 dp cycles)
static void step_mac(int n) {
    step_dp(n * 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// TUE APB transactions (via tb_tue_* backdoor)
// APB protocol: setup phase (psel=1, penable=0) + access phase (psel=1, penable=1)
// TUE has pready=1 always (no wait states).
// ─────────────────────────────────────────────────────────────────────────────

static void apb_write(uint32_t addr, uint32_t data) {
    // Setup phase
    g_top->tb_tue_psel    = 1;
    g_top->tb_tue_penable = 0;
    g_top->tb_tue_pwrite  = 1;
    g_top->tb_tue_paddr   = addr & 0xFFF;
    g_top->tb_tue_pwdata  = data;
    step_ctrl(1);
    // Access phase: data latched at posedge clk_ctrl when psel && penable && pwrite
    g_top->tb_tue_penable = 1;
    step_ctrl(1);
    // Deassert
    g_top->tb_tue_psel    = 0;
    g_top->tb_tue_penable = 0;
    g_top->tb_tue_pwrite  = 0;
}

static uint32_t apb_read(uint32_t addr) {
    g_top->tb_tue_psel    = 1;
    g_top->tb_tue_penable = 0;
    g_top->tb_tue_pwrite  = 0;
    g_top->tb_tue_paddr   = addr & 0xFFF;
    step_ctrl(1);
    g_top->tb_tue_penable = 1;
    step_ctrl(1);
    uint32_t data = g_top->tb_tue_prdata;
    g_top->tb_tue_psel    = 0;
    g_top->tb_tue_penable = 0;
    return data;
}

// Wait for TUE transaction to complete.
// The TUE FSM takes ~36 ctrl cycles (1 IDLE + 33 WAIT_DRAIN + 1 APPLY + 1 DONE).
// We wait 60 ctrl cycles to be safe, then extra dp cycles for apply_pulse_dp sync.
static void tue_wait_done() {
    step_ctrl(60);   // more than enough (36 cycles needed)
    step_dp(16);     // extra margin for 2-FF clk_ctrl→clk_dp synchronizer
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser TCAM programming (via tb_parser_wr_* backdoor)
//
// Parser TCAM entry format (640 bits):
//   [639:634] key_state      (6b) — FSM state to match
//   [633:570] key_window     (64b) — byte window to match (all 0 = don't care with full mask)
//   [569:564] mask_state     (6b)  — 0=match exactly
//   [563:506] padding        (58b)
//   [505:442] mask_window    (64b) — 1=don't care (full = 0xFFFF_FFFF_FFFF_FFFF)
//   [441:436] next_state     (6b)
//   [435:428] extract_offset (8b)  — absolute byte offset in cell to extract
//   [427:420] extract_len    (8b)  — (effectively 1; parser extracts 1 byte per entry)
//   [419:410] phv_dst_offset (10b) — destination byte in PHV
//   [409:402] hdr_advance    (8b)
//   [401]     valid
//   [400:0]   reserved
//
// In Verilator, tb_parser_wr_data[639:0] is VlWide<20> (word[0]=bits31:0, etc.)
// ─────────────────────────────────────────────────────────────────────────────

static void set_bits(uint32_t *words, int hi, int lo, uint64_t value) {
    for (int bit = lo; bit <= hi; bit++) {
        int w = bit / 32;
        int b = bit % 32;
        if ((value >> (uint64_t)(bit - lo)) & 1ULL)
            words[w] |= (1U << b);
        else
            words[w] &= ~(1U << b);
    }
}

// Build a parser TCAM entry: match state=key_state (exact), wildcard window
// extract 1 byte from extract_offset in cell → phv_dst in PHV
// transition to next_state
static void make_parser_entry(uint32_t e[20],
                               uint8_t  key_state,
                               uint8_t  next_state,
                               uint8_t  extract_offset,
                               uint16_t phv_dst)
{
    memset(e, 0, 20 * sizeof(uint32_t));
    set_bits(e, 639, 634, key_state);               // key_state = FSM state
    // key_window = 0, mask_state = 0 (exact state match)
    set_bits(e, 505, 442, 0xFFFFFFFFFFFFFFFFULL);   // mask_window = full don't care
    set_bits(e, 441, 436, next_state);              // next_state
    set_bits(e, 435, 428, extract_offset);          // extract_offset (absolute cell byte)
    set_bits(e, 427, 420, 1);                       // extract_len = 1
    set_bits(e, 419, 410, phv_dst);                 // phv_dst_offset
    set_bits(e, 409, 402, 0);                       // hdr_advance = 0
    e[401 / 32] |= (1U << (401 % 32));              // valid = 1
}

// Write one parser TCAM entry via tb_parser_wr_* backdoor
static void write_parser_entry(uint8_t addr, const uint32_t e[20]) {
    g_top->tb_parser_wr_en   = 1;
    g_top->tb_parser_wr_addr = addr;
    for (int i = 0; i < 20; i++)
        g_top->tb_parser_wr_data[i] = e[i];
    step_dp(2);     // hold for 2 dp cycles (parser latches on posedge clk_dp)
    g_top->tb_parser_wr_en = 0;
    step_dp(2);
}

// ─────────────────────────────────────────────────────────────────────────────
// HAL implementation — converts firmware TCAM entries to RTL format
//
// The RTL MAU TCAM matches on PHV[0:511] (512 bits = 64 bytes).
// The firmware stores its key at key.bytes[0:key_len-1].
// The parser is configured per-test to extract the right packet fields
// into PHV bytes 0:key_len-1, so they align with the firmware's key.
//
// Mask conversion (firmware → RTL):
//   firmware: mask byte=0xFF → must match, 0x00 → don't care
//   RTL:      mask byte=0x00 → must match, 0xFF → don't care
//   → rtl_mask[i] = ~fw_mask[i]
//
// Action ID conversion (firmware → RTL mau_alu.sv op_type):
//   ACTION_FORWARD  (0x1001) → 0xA000  (OP_SET_PORT, imm_val=port)
//   ACTION_DENY     (0x2002) → 0x9000  (OP_DROP)
//   ACTION_PERMIT   (0x2001) → 0x0000  (OP_NOP)
//   ACTION_L2_FORWARD(0x3001)→ 0xA000  (OP_SET_PORT, imm_val=port)
//
// ALU param encoding for OP_SET_PORT:
//   imm_val = action_params[47:16] = ASRAM[47:16]
//   ASRAM[47:0] = {16'b0, dp_action_params[95:0]}[47:0] = P0[31:0]
//   → P0 = port → ACTION_P0 = port
// ─────────────────────────────────────────────────────────────────────────────

static uint16_t fw_to_rtl_action_id(uint16_t fw_id) {
    switch (fw_id) {
    case ACTION_FORWARD:    return 0xA000;  // OP_SET_PORT
    case ACTION_DROP:       return 0x9000;  // OP_DROP
    case ACTION_PERMIT:     return 0x0000;  // OP_NOP
    case ACTION_DENY:       return 0x9000;  // OP_DROP
    case ACTION_L2_FORWARD: return 0xA000;  // OP_SET_PORT
    case ACTION_FLOOD:      return 0xA000;  // OP_SET_PORT (port=0xFF)
    default:                return 0x0000;
    }
}

static uint32_t fw_to_rtl_p0(uint16_t fw_id, const uint8_t *params) {
    switch (fw_id) {
    case ACTION_FORWARD:
    case ACTION_L2_FORWARD:
        // action_params[47:16] = imm_val = {P1[15:0], P0[31:16]}
        // Port is a 5-bit value; place it in P0[31:16] so imm_val = port.
        return (uint32_t)params[0] << 16;
    case ACTION_FLOOD:
        return 0xFFU << 16;
    default:
        return 0;
    }
}

// hal_tcam_insert: called by firmware (route_add, fdb_add_static, etc.)
int hal_tcam_insert(const tcam_entry_t *entry) {
    if (!entry) return HAL_ERR_INVAL;
    if (entry->stage >= 7) return HAL_ERR_INVAL;

    uint16_t rtl_action_id = fw_to_rtl_action_id(entry->action_id);
    uint32_t rtl_p0        = fw_to_rtl_p0(entry->action_id, entry->action_params);

    // Write CMD, TABLE_ID, STAGE
    apb_write(TUE_REG_CMD,      TUE_CMD_INSERT);
    apb_write(TUE_REG_TABLE_ID, entry->table_id);
    apb_write(TUE_REG_STAGE,    entry->stage);

    // Write 512-bit key (16 × 32-bit words)
    // Firmware key.bytes[i] → RTL TCAM key[i] (PHV byte i)
    // Parser is configured to extract the right packet bytes to PHV[0:key_len-1]
    for (int w = 0; w < 16; w++) {
        uint32_t word = 0;
        for (int b = 0; b < 4; b++) {
            int idx = w * 4 + b;
            if (idx < 64 && idx < (int)entry->key.key_len)
                word |= ((uint32_t)entry->key.bytes[idx]) << (b * 8);
        }
        apb_write(TUE_REG_KEY_BASE + (uint32_t)(w * 4), word);
    }

    // Write 512-bit mask (16 × 32-bit words)
    // Firmware mask convention (1=must match) → RTL convention (1=don't care)
    // Invert: rtl_mask = ~fw_mask
    // Bytes beyond key_len default to 0xFF in firmware → RTL: ~0xFF = 0x00 = must match
    // But we want them as don't care → set explicitly to 0xFF (RTL don't care)
    for (int w = 0; w < 16; w++) {
        uint32_t word = 0xFFFFFFFFU;   // default: all don't care
        for (int b = 0; b < 4; b++) {
            int idx = w * 4 + b;
            if (idx < (int)entry->mask.key_len) {
                uint8_t fw_m = entry->mask.bytes[idx];
                uint8_t rtl_m = (uint8_t)(~fw_m);
                // Place rtl_m at byte position b in the word
                word &= ~(0xFFU << (b * 8));
                word |= ((uint32_t)rtl_m) << (b * 8);
            }
        }
        apb_write(TUE_REG_MASK_BASE + (uint32_t)(w * 4), word);
    }

    // Write action
    apb_write(TUE_REG_ACTION_ID, rtl_action_id);
    apb_write(TUE_REG_ACTION_P0, rtl_p0);
    apb_write(TUE_REG_ACTION_P1, 0);
    apb_write(TUE_REG_ACTION_P2, 0);

    // Commit — triggers TUE state machine
    apb_write(TUE_REG_COMMIT, 1);

    // Wait for completion (~36 ctrl cycles + clock domain crossing)
    tue_wait_done();

    return HAL_OK;
}

int hal_tcam_delete(uint8_t stage, uint16_t table_id) {
    // Write CMD=DELETE, TABLE_ID, STAGE, COMMIT
    apb_write(TUE_REG_CMD,      TUE_CMD_DELETE);
    apb_write(TUE_REG_TABLE_ID, table_id);
    apb_write(TUE_REG_STAGE,    stage);
    apb_write(TUE_REG_COMMIT,   1);
    tue_wait_done();
    return HAL_OK;
}

int hal_tcam_modify(const tcam_entry_t *entry) {
    return hal_tcam_insert(entry);  // INSERT overwrites existing entry at same table_id
}

int hal_tcam_flush(uint8_t stage) {
    // Write CMD=FLUSH, STAGE, COMMIT — clears all entries in the stage
    apb_write(TUE_REG_CMD,    TUE_CMD_FLUSH);
    apb_write(TUE_REG_STAGE,  stage);
    apb_write(TUE_REG_COMMIT, 1);
    tue_wait_done();
    return HAL_OK;
}

// Stub HAL functions (non-TCAM operations — no RTL counterpart in this design)
int hal_init(void)                                          { return HAL_OK; }
int hal_counter_read(counter_id_t, uint64_t *b, uint64_t *p) { if(b)*b=0; if(p)*p=0; return HAL_OK; }
int hal_counter_reset(counter_id_t)                        { return HAL_OK; }
int hal_meter_config(meter_id_t, const meter_cfg_t *)      { return HAL_OK; }
int hal_parser_add_state(const fsm_entry_t *)              { return HAL_OK; }
int hal_parser_del_state(uint8_t)                          { return HAL_OK; }
int hal_port_enable(port_id_t)                             { return HAL_OK; }
int hal_port_disable(port_id_t)                            { return HAL_OK; }
int hal_port_stats(port_id_t, port_stats_t *s)             { if(s)memset(s,0,sizeof(*s)); return HAL_OK; }
int hal_port_stats_clear(port_id_t)                        { return HAL_OK; }
int hal_vlan_pvid_set(port_id_t, uint16_t)                 { return HAL_OK; }
int hal_vlan_mode_set(port_id_t, uint8_t)                  { return HAL_OK; }
int hal_vlan_member_set(uint16_t, uint32_t, uint32_t)      { return HAL_OK; }
uint32_t hal_vlan_member_get(uint16_t)                     { return 0; }
int hal_qos_dwrr_set(port_id_t, uint8_t, uint32_t)        { return HAL_OK; }
int hal_qos_pir_set(port_id_t, uint64_t)                  { return HAL_OK; }
int hal_qos_sched_mode_set(port_id_t, uint8_t)            { return HAL_OK; }
int hal_qos_dscp_map_set(uint8_t, uint8_t)                { return HAL_OK; }
int hal_punt_rx_poll(punt_pkt_t *p)                        { if(p)memset(p,0,sizeof(*p)); return -1; }
int hal_punt_tx_send(const punt_pkt_t *)                   { return HAL_OK; }
int hal_uart_putc(char c)                                  { putchar(c); return 0; }
int hal_uart_getc(void)                                    { return -1; }
void hal_uart_puts(const char *s)                          { fputs(s, stdout); }

// ─────────────────────────────────────────────────────────────────────────────
// Packet injection utilities
// ─────────────────────────────────────────────────────────────────────────────

// Build a minimal IPv4 packet (Ethernet + IPv4 header, no L4 payload)
// Returns length (34 bytes)
static int build_ipv4_pkt(uint8_t *buf,
                           const uint8_t *eth_dst, const uint8_t *eth_src,
                           uint32_t src_ip, uint32_t dst_ip,
                           uint8_t proto, uint16_t dport)
{
    int off = 0;
    memcpy(buf + off, eth_dst, 6); off += 6;
    memcpy(buf + off, eth_src, 6); off += 6;
    buf[off++] = 0x08; buf[off++] = 0x00;            // EtherType = IPv4
    buf[off++] = 0x45;                                 // Ver=4, IHL=5
    buf[off++] = 0x00;                                 // TOS
    buf[off++] = 0x00; buf[off++] = (dport ? 24 : 20); // Total length
    buf[off++] = 0x00; buf[off++] = 0x01;             // ID
    buf[off++] = 0x00; buf[off++] = 0x00;             // Flags+Frag
    buf[off++] = 64;                                   // TTL
    buf[off++] = proto;                                // Protocol
    buf[off++] = 0x00; buf[off++] = 0x00;             // Checksum
    buf[off++] = (src_ip >> 24) & 0xFF;
    buf[off++] = (src_ip >> 16) & 0xFF;
    buf[off++] = (src_ip >>  8) & 0xFF;
    buf[off++] =  src_ip        & 0xFF;
    buf[off++] = (dst_ip >> 24) & 0xFF;
    buf[off++] = (dst_ip >> 16) & 0xFF;
    buf[off++] = (dst_ip >>  8) & 0xFF;
    buf[off++] =  dst_ip        & 0xFF;
    if (dport) {
        buf[off++] = 0x00; buf[off++] = 0x50;        // src port = 80
        buf[off++] = (dport >> 8) & 0xFF;
        buf[off++] =  dport       & 0xFF;
    }
    return off;
}

// Build a pure L2 frame (18 bytes: 14 Ethernet + 4 payload)
static int build_l2_pkt(uint8_t *buf,
                         const uint8_t *eth_dst, const uint8_t *eth_src,
                         uint16_t ethertype)
{
    memcpy(buf,     eth_dst, 6);
    memcpy(buf + 6, eth_src, 6);
    buf[12] = (ethertype >> 8) & 0xFF;
    buf[13] =  ethertype       & 0xFF;
    buf[14] = buf[15] = buf[16] = buf[17] = 0;
    return 18;
}

// Fill rx_data port 0 with packet bytes
// Verilator packs rx_data[31:0][511:0] as a flat VlWide<512>.
// Port 0 occupies words [15:0] (bits 511:0).
// Byte b of the packet → word b/4, bit position (b%4)*8.
static void fill_rx_data_port0(const uint8_t *pkt, int len) {
    // Clear port 0 portion (words 0-15)
    for (int w = 0; w < 16; w++)
        g_top->rx_data[w] = 0;
    for (int b = 0; b < len && b < 64; b++)
        g_top->rx_data[b / 4] |= ((uint32_t)pkt[b]) << ((b % 4) * 8);
}

// Inject one packet on port 0 (via MAC RX interface)
// Hold for enough mac cycles for the arbiter and parser to accept it
static void inject_pkt(const uint8_t *pkt, int len) {
    // Set eop_len: rx_eop_len[31:0][6:0] is VlWide<7>. Port 0 at bits 6:0 (word 0 bits 6:0).
    g_top->rx_eop_len[0] = (g_top->rx_eop_len[0] & ~0x7FU) | ((uint32_t)len & 0x7F);
    fill_rx_data_port0(pkt, len);

    // Assert SOF+EOF (single-cell packet, entire frame fits in 64 bytes)
    g_top->rx_valid = 1U;         // port 0 valid
    g_top->rx_sof   = 1U;         // port 0 SOF
    g_top->rx_eof   = 1U;         // port 0 EOF
    g_top->tx_ready = 0xFFFFFFFFU; // accept TX on all ports

    // Hold for 10 dp cycles (≈2.5 mac cycles) so parser can latch
    step_dp(10);

    // Wait for rx_ready[0] (arbiter accepted the frame)
    for (int w = 0; w < 200; w++) {
        if (g_top->rx_ready & 1U) break;
        step_dp(1);
    }

    // Deassert
    g_top->rx_valid = 0;
    g_top->rx_sof   = 0;
    g_top->rx_eof   = 0;
}

// Poll TX for up to max_dp_cycles dp cycles; return the first non-zero tx_valid mask
// Returns 0 if timeout (no TX seen)
static uint32_t poll_tx(int max_dp_cycles) {
    for (int i = 0; i < max_dp_cycles; i++) {
        step_dp(1);
        uint32_t tv = g_top->tx_valid;
        if (tv) return tv;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset sequence
// ─────────────────────────────────────────────────────────────────────────────

static void do_reset() {
    g_top->rst_n = 0;
    // Zero all inputs
    g_top->rx_valid      = 0;
    g_top->rx_sof        = 0;
    g_top->rx_eof        = 0;
    g_top->tx_ready      = 0xFFFFFFFFU;
    g_top->tb_parser_wr_en   = 0;
    g_top->tb_parser_wr_addr = 0;
    for (int i = 0; i < 20; i++) g_top->tb_parser_wr_data[i] = 0;
    g_top->tb_tue_psel    = 0;
    g_top->tb_tue_penable = 0;
    g_top->tb_tue_pwrite  = 0;
    g_top->tb_tue_paddr   = 0;
    g_top->tb_tue_pwdata  = 0;
    g_top->pcie_rx_valid  = 0;
    g_top->pcie_rx_data[0] = 0; // sufficient to clear valid-sensitive signals

    step_dp(20);     // hold reset for 20 dp cycles
    g_top->rst_n = 1;
    step_dp(20);     // allow reset synchronizers to propagate
}

// ─────────────────────────────────────────────────────────────────────────────
// Test framework macros
// ─────────────────────────────────────────────────────────────────────────────

#define TEST_BEGIN(name)  printf("  [ RUN ] %s\n", (name))
#define TEST_PASS(name)   do { printf("  [PASS ] %s\n", (name)); g_pass++; } while(0)
#define TEST_FAIL(name, fmt, ...) \
    do { printf("  [FAIL ] %s — " fmt "\n", (name), ##__VA_ARGS__); g_fail++; } while(0)

// ─────────────────────────────────────────────────────────────────────────────
// CS-RTL-1: IPv4 LPM routing
//
// Setup:
//   Parser: extract IPv4 DST (packet bytes 30-33) → PHV[0:3]
//           (matches firmware route.c key.bytes[0:3] = IPv4 prefix)
//   TUE: program Stage 0 via route_add(10.10.0.0/16, port=3, mac=...)
//   Mask conversion: /16 → fw_mask=[0xFF,0xFF,0x00,0x00]
//                        → rtl_mask=[0x00,0x00,0xFF,0xFF]
//                   → RTL word0 mask = 0xFFFF0000
//
// Expect: tx_valid[3] goes high (packet exits on port 3)
// ─────────────────────────────────────────────────────────────────────────────

static void test_rtl_route_forward() {
    const char *name = "CS-RTL-1 : IPv4 LPM routing → TX port 3";
    TEST_BEGIN(name);

    do_reset();
    route_init();

    // Parser setup: 4 entries, states 1→2→3→4→ACCEPT
    // Each entry extracts 1 byte from IPv4 DST (packet bytes 30-33) into PHV[0:3]
    // IPv4 DST byte 0 = packet byte 30 (14 ETH + 16 IPv4 hdr offset = 30)
    for (int i = 0; i < 4; i++) {
        uint32_t e[20];
        uint8_t  ns = (i == 3) ? 0x3F : (uint8_t)(i + 2);  // ACCEPT=0x3F after 4th
        make_parser_entry(e, (uint8_t)(i + 1), ns,
                          (uint8_t)(30 + i),  // extract_offset: packet bytes 30-33
                          (uint16_t)i);        // phv_dst: PHV bytes 0-3
        write_parser_entry((uint8_t)i, e);
    }

    // Install route: 10.10.0.0/16 → port 3, next-hop MAC = AA:BB:CC:DD:EE:FF
    uint64_t nhop_mac = 0xAABBCCDDEEFFULL;
    int rc = route_add(0x0A0A0000u, 16, 3, nhop_mac);
    if (rc != 0) {
        TEST_FAIL(name, "route_add returned %d", rc);
        return;
    }

    // Build and inject packet: IPv4 dst = 10.10.5.99
    static const uint8_t eth_dst[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t eth_src[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t pkt[64] = {};
    int len = build_ipv4_pkt(pkt, eth_dst, eth_src,
                              0x01020304u,   // src = 1.2.3.4
                              0x0A0A0563u,   // dst = 10.10.5.99
                              0, 0);
    inject_pkt(pkt, len);

    // Poll for TX — expect packet on port 3 within 2000 dp cycles
    uint32_t tv = poll_tx(2000);

    if (tv & (1U << 3))
        TEST_PASS(name);
    else if (tv)
        TEST_FAIL(name, "TX on wrong port(s): mask=0x%08X (expected bit 3)", tv);
    else
        TEST_FAIL(name, "timeout — no TX output after 2000 dp cycles");
}

// ─────────────────────────────────────────────────────────────────────────────
// CS-RTL-2: L2 FDB forwarding
//
// Setup:
//   Parser: extract ETH_DST (packet bytes 0-5) → PHV[0:5]
//           (matches firmware fdb.c key.bytes[0:5] = destination MAC)
//   TUE: program Stage 2 via fdb_add_static(DE:AD:BE:EF:00:01, port=7)
//   Exact match: fw_mask=0xFF → rtl_mask=0x00 for all 6 bytes
//
// Expect: tx_valid[7] goes high (packet exits on port 7)
// ─────────────────────────────────────────────────────────────────────────────

static void test_rtl_fdb_forward() {
    const char *name = "CS-RTL-2 : L2 FDB forwarding → TX port 7";
    TEST_BEGIN(name);

    do_reset();
    fdb_init();

    // Parser setup: 6 entries, states 1→2→3→4→5→6→ACCEPT
    // Extract ETH_DST (packet bytes 0-5) into PHV[0:5]
    for (int i = 0; i < 6; i++) {
        uint32_t e[20];
        uint8_t  ns = (i == 5) ? 0x3F : (uint8_t)(i + 2);
        make_parser_entry(e, (uint8_t)(i + 1), ns,
                          (uint8_t)i,         // extract_offset: packet bytes 0-5
                          (uint16_t)i);        // phv_dst: PHV bytes 0-5
        write_parser_entry((uint8_t)i, e);
    }

    // Install FDB: DE:AD:BE:EF:00:01 → port 7
    int rc = fdb_add_static(0xDEADBEEF0001ULL, 7, 0);
    if (rc != 0) {
        TEST_FAIL(name, "fdb_add_static returned %d", rc);
        return;
    }

    // Build and inject L2 packet with known dst MAC
    static const uint8_t known_mac[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};
    static const uint8_t src_mac[6]   = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    uint8_t pkt[18] = {};
    int len = build_l2_pkt(pkt, known_mac, src_mac, 0x9999);
    inject_pkt(pkt, len);

    uint32_t tv = poll_tx(2000);

    if (tv & (1U << 7))
        TEST_PASS(name);
    else if (tv)
        TEST_FAIL(name, "TX on wrong port(s): mask=0x%08X (expected bit 7)", tv);
    else
        TEST_FAIL(name, "timeout — no TX output after 2000 dp cycles");
}

// ─────────────────────────────────────────────────────────────────────────────
// CS-RTL-3: ACL deny (drop packet)
//
// Setup:
//   Parser: extract IPv4 SRC (packet bytes 26-29) → PHV[0:3]
//           (matches firmware acl.c key.bytes[0:3] = IPv4 SRC prefix)
//   TUE: program Stage 1 via acl_add_deny(172.16.0.0, mask=0xFFF00000, ...)
//   No routing entry → default pass-through at Stage 0
//   ACL deny at Stage 1 → OP_DROP → meta.drop=1 → TM discards packet
//
// Expect: no TX output within timeout (packet dropped)
// ─────────────────────────────────────────────────────────────────────────────

static void test_rtl_acl_deny() {
    const char *name = "CS-RTL-3 : ACL deny → no TX output (packet dropped)";
    TEST_BEGIN(name);

    do_reset();
    acl_init();

    // Parser setup: 4 entries, states 1→2→3→4→ACCEPT
    // Extract IPv4 SRC (packet bytes 26-29) into PHV[0:3]
    // IPv4 SRC = packet bytes 26-29 (14 ETH + 12 IPv4 SRC offset = 26)
    for (int i = 0; i < 4; i++) {
        uint32_t e[20];
        uint8_t  ns = (i == 3) ? 0x3F : (uint8_t)(i + 2);
        make_parser_entry(e, (uint8_t)(i + 1), ns,
                          (uint8_t)(26 + i),  // extract_offset: packet bytes 26-29
                          (uint16_t)i);        // phv_dst: PHV bytes 0-3
        write_parser_entry((uint8_t)i, e);
    }

    // Install ACL deny: src 172.16.0.0/12 → deny
    // acl_add_deny(src_ip, src_mask, dst_ip, dst_mask, dport)
    int rid = acl_add_deny(0xAC100000u, 0xFFF00000u, 0, 0, 0);
    if (rid < 0) {
        TEST_FAIL(name, "acl_add_deny returned %d", rid);
        return;
    }

    // Build and inject packet: src=172.16.1.2 (matches 172.16.0.0/12)
    static const uint8_t eth_dst[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static const uint8_t eth_src[6] = {0x00,0xAA,0xBB,0xCC,0xDD,0xEE};
    uint8_t pkt[64] = {};
    int len = build_ipv4_pkt(pkt, eth_dst, eth_src,
                              0xAC100102u,   // src = 172.16.1.2 (matches /12)
                              0xC0A80001u,   // dst = 192.168.0.1
                              6, 80);
    inject_pkt(pkt, len);

    // Poll TX: expect NO output within 1000 dp cycles
    uint32_t tv = poll_tx(1000);

    if (tv == 0)
        TEST_PASS(name);
    else
        TEST_FAIL(name, "unexpected TX on port(s): mask=0x%08X (expected no TX)", tv);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    printf("RV-P4 RTL Co-Simulation\n");
    printf("========================\n");
    printf("Data plane : Verilator RTL (rv_p4_top)\n");
    printf("Control plane : C firmware (route_add, fdb_add_static, acl_add_deny)\n");
    printf("Bridge : TUE APB via tb_tue_* backdoor ports\n");
    printf("========================\n\n");

    // Create Verilator model
    g_top = new Vrv_p4_top;

    // Initialize clocks (start at 0)
    g_top->clk_dp   = 0;
    g_top->clk_ctrl = 0;
    g_top->clk_mac  = 0;
    g_top->clk_cpu  = 0;
    g_top->rst_n    = 0;
    g_top->eval();

    printf("[ SUITE ] RTL Data-Plane Co-Simulation (3 cases)\n\n");

    test_rtl_route_forward();
    test_rtl_fdb_forward();
    test_rtl_acl_deny();

    // Summary
    int total = g_pass + g_fail;
    printf("\n========================\n");
    printf("Results: %d/%d passed", g_pass, total);
    if (g_fail == 0)
        printf("  ALL PASS\n");
    else
        printf("  %d FAILED\n", g_fail);
    printf("========================\n");

    g_top->final();
    delete g_top;
    return (g_fail == 0) ? 0 : 1;
}
