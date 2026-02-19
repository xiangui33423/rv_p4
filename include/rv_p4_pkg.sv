// rv_p4_pkg.sv — 全局参数、类型、结构体定义
// RV-P4 Switch ASIC

`ifndef RV_P4_PKG_SV
`define RV_P4_PKG_SV

package rv_p4_pkg;

// ─────────────────────────────────────────────
// 全局参数
// ─────────────────────────────────────────────
parameter int NUM_PORTS       = 32;    // 物理端口数
parameter int NUM_MAU_STAGES  = 24;    // MAU 流水级数（16 ingress + 8 egress）
parameter int NUM_QUEUES      = 256;   // TM 队列总数（32 端口 × 8 队列）

// PHV
parameter int PHV_BYTES       = 512;
parameter int PHV_BITS        = PHV_BYTES * 8;  // 4096b

// Parser
parameter int PARSER_FSM_STATES = 64;
parameter int PARSER_TCAM_DEPTH = 256;
parameter int PARSER_TCAM_WIDTH = 640;

// MAU 每级资源
parameter int MAU_TCAM_DEPTH    = 2048;
parameter int MAU_TCAM_KEY_W    = 512;
parameter int MAU_ASRAM_DEPTH   = 65536;  // Action SRAM
parameter int MAU_ASRAM_WIDTH   = 128;
parameter int MAU_SSRAM_BYTES   = 262144; // Stateful SRAM 256KiB

// 包缓冲
parameter int PKT_BUF_BYTES     = 64 * 1024 * 1024; // 64MiB
parameter int CELL_BYTES        = 64;
parameter int CELL_DATA_BYTES   = 60;
parameter int NUM_CELLS         = PKT_BUF_BYTES / CELL_BYTES; // 1M
parameter int CELL_ID_W         = $clog2(NUM_CELLS);           // 20b

// TUE
parameter int TUE_QUEUE_DEPTH   = 64;

// ─────────────────────────────────────────────
// PHV 字段偏移（字节）
// ─────────────────────────────────────────────
parameter int PHV_OFF_ETH_DST   = 0;    // 6B
parameter int PHV_OFF_ETH_SRC   = 6;    // 6B
parameter int PHV_OFF_ETH_TYPE  = 12;   // 2B
parameter int PHV_OFF_VLAN      = 14;   // 4B
parameter int PHV_OFF_IPV4_SRC  = 34;   // 4B  (以太网14 + VLAN4 + IPv4偏移12)
parameter int PHV_OFF_IPV4_DST  = 38;   // 4B
parameter int PHV_OFF_IPV4_TTL  = 30;   // 1B
parameter int PHV_OFF_IPV4_PROTO= 31;   // 1B
parameter int PHV_OFF_TCP_SPORT = 54;   // 2B
parameter int PHV_OFF_TCP_DPORT = 56;   // 2B
parameter int PHV_OFF_UDP_SPORT = 54;   // 2B
parameter int PHV_OFF_UDP_DPORT = 56;   // 2B

// 元数据区偏移（字节，相对 PHV 起始）
parameter int PHV_OFF_IG_PORT   = 256;
parameter int PHV_OFF_EG_PORT   = 257;
parameter int PHV_OFF_DROP      = 258;
parameter int PHV_OFF_PRIORITY  = 259;
parameter int PHV_OFF_FLOW_HASH = 260;  // 4B
parameter int PHV_OFF_TIMESTAMP = 264;  // 8B

// ─────────────────────────────────────────────
// 类型定义
// ─────────────────────────────────────────────

// PHV 元数据（随 PHV 总线传递的控制信息）
typedef struct packed {
    logic [7:0]  ig_port;       // 入端口
    logic [7:0]  eg_port;       // 出端口
    logic        drop;          // 丢包标志
    logic        slow_path;     // 慢速路径（保留，当前架构不使用）
    logic        multicast;     // 组播
    logic        mirror;        // 镜像
    logic [2:0]  priority;      // QoS 优先级
    logic [15:0] pkt_len;       // 包长度（字节）
    logic [CELL_ID_W-1:0] cell_id; // 包缓冲首 cell ID
    logic [63:0] timestamp;     // 入包时间戳（ns）
    logic [31:0] flow_hash;     // 流哈希
    logic [7:0]  mau_stage;     // 当前 MAU 级（调试用）
} phv_meta_t;

// MAU TCAM 条目
typedef struct packed {
    logic [MAU_TCAM_KEY_W-1:0] key;
    logic [MAU_TCAM_KEY_W-1:0] mask;
    logic [15:0]               action_id;
    logic [15:0]               action_ptr;  // Action SRAM 索引
    logic                      valid;
} mau_tcam_entry_t;

// MAU Action SRAM 条目（128b）
typedef struct packed {
    logic [15:0] action_id;
    logic [111:0] params;       // 动作参数（最多 14 字节）
} mau_action_t;

// Stateful SRAM 操作类型
typedef enum logic [1:0] {
    STAT_OP_READ  = 2'b00,
    STAT_OP_WRITE = 2'b01,
    STAT_OP_ADD   = 2'b10,
    STAT_OP_CAS   = 2'b11
} stat_op_t;

// TUE 操作类型
typedef enum logic [1:0] {
    TUE_OP_INSERT = 2'b00,
    TUE_OP_DELETE = 2'b01,
    TUE_OP_MODIFY = 2'b10,
    TUE_OP_FLUSH  = 2'b11
} tue_op_t;

// TUE 请求
typedef struct packed {
    tue_op_t               op;
    logic [4:0]            stage;       // 目标 MAU 级（0-23）
    logic [15:0]           table_id;
    logic [MAU_TCAM_KEY_W-1:0] key;
    logic [MAU_TCAM_KEY_W-1:0] mask;
    logic [15:0]           action_id;
    logic [111:0]          action_params;
} tue_req_t;

// 包缓冲 Cell 标志
typedef struct packed {
    logic sof;          // 包起始
    logic eof;          // 包结束
    logic multicast;    // 组播（引用计数管理）
    logic [4:0] rsvd;
} cell_flags_t;

// Cell 所有权状态
typedef enum logic [1:0] {
    CELL_FREE     = 2'b00,
    CELL_RX_WRITE = 2'b01,
    CELL_OWNED    = 2'b10,
    CELL_TX_READ  = 2'b11
} cell_state_t;

// TM 队列调度类型
typedef enum logic [1:0] {
    SCHED_SP   = 2'b00,   // 严格优先级
    SCHED_DWRR = 2'b01,   // 赤字加权轮询
    SCHED_WFQ  = 2'b10    // 加权公平队列
} sched_type_t;

// ─────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────
parameter logic [15:0] ETHERTYPE_IPV4  = 16'h0800;
parameter logic [15:0] ETHERTYPE_IPV6  = 16'h86DD;
parameter logic [15:0] ETHERTYPE_ARP   = 16'h0806;
parameter logic [15:0] ETHERTYPE_VLAN  = 16'h8100;
parameter logic [15:0] ETHERTYPE_QINQ  = 16'h88A8;
parameter logic [15:0] ETHERTYPE_MPLS  = 16'h8847;

parameter logic [7:0]  IP_PROTO_TCP    = 8'd6;
parameter logic [7:0]  IP_PROTO_UDP    = 8'd17;
parameter logic [7:0]  IP_PROTO_ICMP   = 8'd1;
parameter logic [7:0]  IP_PROTO_OSPF   = 8'd89;

// TUE APB 基地址偏移
parameter logic [11:0] TUE_REG_CMD          = 12'h000;
parameter logic [11:0] TUE_REG_TABLE_ID     = 12'h004;
parameter logic [11:0] TUE_REG_KEY_LO       = 12'h008;
parameter logic [11:0] TUE_REG_KEY_HI       = 12'h00C;
parameter logic [11:0] TUE_REG_MASK_LO      = 12'h010;
parameter logic [11:0] TUE_REG_MASK_HI      = 12'h014;
parameter logic [11:0] TUE_REG_ACTION       = 12'h018;
parameter logic [11:0] TUE_REG_STATUS       = 12'h01C;
parameter logic [11:0] TUE_REG_BATCH_COMMIT = 12'h020;

endpackage

`endif // RV_P4_PKG_SV
