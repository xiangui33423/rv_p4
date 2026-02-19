// rv_p4_pkg.sv
// 全局参数、类型、结构体定义
// RV-P4 Switch ASIC — 统一 C 编程模型 + P4 硬件流水线

`ifndef RV_P4_PKG_SV
`define RV_P4_PKG_SV

package rv_p4_pkg;

// ─────────────────────────────────────────────
// 顶层参数
// ─────────────────────────────────────────────
parameter int NUM_PORTS         = 32;
parameter int NUM_MAU_STAGES    = 24;   // 16 ingress + 8 egress
parameter int NUM_QUEUES        = 256;  // 32 端口 × 8 队列

// PHV
parameter int PHV_BYTES         = 512;
parameter int PHV_BITS          = PHV_BYTES * 8;  // 4096b

// Parser
parameter int PARSER_FSM_STATES = 64;
parameter int PARSER_TCAM_DEPTH = 256;
parameter int PARSER_TCAM_WIDTH = 640;  // key+mask+next_state+extract

// MAU 每级
parameter int MAU_TCAM_DEPTH    = 2048;
parameter int MAU_TCAM_KEY_W    = 512;
parameter int MAU_ASRAM_DEPTH   = 65536;
parameter int MAU_ASRAM_WIDTH   = 128;
parameter int MAU_SSRAM_BYTES   = 262144;  // 256 KiB

// 包缓冲
parameter int CELL_BYTES        = 64;
parameter int CELL_DATA_BYTES   = 60;
parameter int NUM_CELLS         = 1048576; // 1M cells = 64 MiB
parameter int CELL_ID_W         = $clog2(NUM_CELLS); // 20b

// ─────────────────────────────────────────────
// PHV 元数据（随 PHV 总线传递）
// ─────────────────────────────────────────────
typedef struct packed {
    logic [4:0]            ig_port;    // 入端口 0-31
    logic [4:0]            eg_port;    // 出端口 0-31
    logic                  drop;
    logic                  multicast;
    logic                  mirror;
    logic [2:0]            qos_prio;   // QoS 0-7
    logic [13:0]           pkt_len;    // 包长度（字节）
    logic [CELL_ID_W-1:0]  cell_id;    // 包缓冲首 cell
    logic [47:0]           timestamp;  // 入包时间戳（ns，48b 足够）
    logic [31:0]           flow_hash;
} phv_meta_t;  // 共 5+5+1+1+1+3+14+20+48+32 = 130b → 对齐到 136b (17B)

// ─────────────────────────────────────────────
// MAU 相关类型
// ─────────────────────────────────────────────
typedef struct packed {
    logic [MAU_TCAM_KEY_W-1:0] key;
    logic [MAU_TCAM_KEY_W-1:0] mask;
    logic [15:0]               action_id;
    logic [15:0]               action_ptr;  // Action SRAM 索引
    logic                      valid;
} mau_tcam_entry_t;

typedef struct packed {
    logic [15:0]  action_id;
    logic [111:0] params;   // 动作参数 14B
} mau_action_t;  // 128b

typedef enum logic [1:0] {
    STAT_OP_READ  = 2'b00,
    STAT_OP_WRITE = 2'b01,
    STAT_OP_ADD   = 2'b10,
    STAT_OP_CAS   = 2'b11
} stat_op_t;

// ─────────────────────────────────────────────
// TUE 相关类型
// ─────────────────────────────────────────────
typedef enum logic [1:0] {
    TUE_INSERT = 2'b00,
    TUE_DELETE = 2'b01,
    TUE_MODIFY = 2'b10,
    TUE_FLUSH  = 2'b11
} tue_op_t;

typedef struct packed {
    tue_op_t                   op;
    logic [4:0]                stage;
    logic [15:0]               table_id;
    logic [MAU_TCAM_KEY_W-1:0] key;
    logic [MAU_TCAM_KEY_W-1:0] mask;
    logic [15:0]               action_id;
    logic [111:0]              action_params;
} tue_req_t;

// ─────────────────────────────────────────────
// 包缓冲 Cell 状态
// ─────────────────────────────────────────────
typedef enum logic [1:0] {
    CELL_FREE     = 2'b00,
    CELL_RX_WRITE = 2'b01,
    CELL_OWNED    = 2'b10,
    CELL_TX_READ  = 2'b11
} cell_state_t;

// ─────────────────────────────────────────────
// 常用 EtherType / IP Proto 常量
// ─────────────────────────────────────────────
parameter logic [15:0] ETYPE_IPV4 = 16'h0800;
parameter logic [15:0] ETYPE_IPV6 = 16'h86DD;
parameter logic [15:0] ETYPE_ARP  = 16'h0806;
parameter logic [15:0] ETYPE_VLAN = 16'h8100;
parameter logic [15:0] ETYPE_MPLS = 16'h8847;

parameter logic [7:0]  PROTO_TCP  = 8'd6;
parameter logic [7:0]  PROTO_UDP  = 8'd17;
parameter logic [7:0]  PROTO_ICMP = 8'd1;

// ─────────────────────────────────────────────
// TUE APB 寄存器偏移
// ─────────────────────────────────────────────
parameter logic [11:0] TUE_REG_CMD          = 12'h000;
parameter logic [11:0] TUE_REG_TABLE_ID     = 12'h004;
parameter logic [11:0] TUE_REG_STAGE        = 12'h008;
parameter logic [11:0] TUE_REG_KEY_0        = 12'h010; // key[31:0]
parameter logic [11:0] TUE_REG_KEY_15       = 12'h04C; // key[511:480]
parameter logic [11:0] TUE_REG_MASK_0       = 12'h050;
parameter logic [11:0] TUE_REG_MASK_15      = 12'h08C;
parameter logic [11:0] TUE_REG_ACTION_ID    = 12'h090;
parameter logic [11:0] TUE_REG_ACTION_P0    = 12'h094;
parameter logic [11:0] TUE_REG_ACTION_P1    = 12'h098;
parameter logic [11:0] TUE_REG_ACTION_P2    = 12'h09C;
parameter logic [11:0] TUE_REG_STATUS       = 12'h0A0;
parameter logic [11:0] TUE_REG_COMMIT       = 12'h0A4;

endpackage

`endif
