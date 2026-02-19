// rv_p4_if.sv
// 全局 Interface 定义
// RV-P4 Switch ASIC

`ifndef RV_P4_IF_SV
`define RV_P4_IF_SV

`include "rv_p4_pkg.sv"
import rv_p4_pkg::*;

// ─────────────────────────────────────────────
// PHV 总线 Interface
// valid/ready 握手 + PHV 数据 + 元数据
// ─────────────────────────────────────────────
interface phv_if (input logic clk, input logic rst_n);
    logic              valid;
    logic [PHV_BITS-1:0] data;
    phv_meta_t         meta;
    logic              ready;

    // 发送端（MAU/Parser 输出侧）
    modport src (
        output valid, data, meta,
        input  ready
    );

    // 接收端（MAU/TM 输入侧）
    modport dst (
        input  valid, data, meta,
        output ready
    );

    // 监控（只读，用于仿真）
    modport monitor (
        input valid, data, meta, ready
    );
endinterface

// ─────────────────────────────────────────────
// APB Interface（控制面 CSR 总线）
// ─────────────────────────────────────────────
interface apb_if (input logic clk, input logic rst_n);
    logic        psel;
    logic        penable;
    logic        pwrite;
    logic [11:0] paddr;
    logic [31:0] pwdata;
    logic [31:0] prdata;
    logic        pready;
    logic        pslverr;

    // APB 主端（ctrl_plane 侧）
    modport master (
        output psel, penable, pwrite, paddr, pwdata,
        input  prdata, pready, pslverr
    );

    // APB 从端（各模块 CSR 侧）
    modport slave (
        input  psel, penable, pwrite, paddr, pwdata,
        output prdata, pready, pslverr
    );
endinterface

// ─────────────────────────────────────────────
// 包缓冲写端口 Interface（Parser → pkt_buffer）
// ─────────────────────────────────────────────
interface pb_wr_if (input logic clk, input logic rst_n);
    logic                  valid;
    logic [CELL_ID_W-1:0]  cell_id;
    logic [511:0]          data;    // 64B cell
    logic                  sof;
    logic                  eof;
    logic [6:0]            data_len; // 本 cell 有效字节数（1-64）
    logic                  ready;

    modport src (
        output valid, cell_id, data, sof, eof, data_len,
        input  ready
    );
    modport dst (
        input  valid, cell_id, data, sof, eof, data_len,
        output ready
    );
endinterface

// ─────────────────────────────────────────────
// 包缓冲读端口 Interface（TM/Deparser → pkt_buffer）
// ─────────────────────────────────────────────
interface pb_rd_if (input logic clk, input logic rst_n);
    // 请求
    logic                  req_valid;
    logic [CELL_ID_W-1:0]  req_cell_id;
    logic                  req_ready;
    // 响应
    logic [511:0]          rsp_data;
    logic                  rsp_valid;
    logic [CELL_ID_W-1:0]  rsp_next_cell_id; // 链表下一 cell
    logic                  rsp_eof;

    modport master (
        output req_valid, req_cell_id,
        input  req_ready,
        input  rsp_data, rsp_valid, rsp_next_cell_id, rsp_eof
    );
    modport slave (
        input  req_valid, req_cell_id,
        output req_ready,
        output rsp_data, rsp_valid, rsp_next_cell_id, rsp_eof
    );
endinterface

// ─────────────────────────────────────────────
// Cell 分配/释放 Interface
// ─────────────────────────────────────────────
interface cell_alloc_if (input logic clk, input logic rst_n);
    // 分配
    logic                  alloc_req;
    logic [CELL_ID_W-1:0]  alloc_id;
    logic                  alloc_valid;  // 分配成功
    logic                  alloc_empty;  // 缓冲耗尽
    // 释放
    logic                  free_req;
    logic [CELL_ID_W-1:0]  free_id;

    modport requester (
        output alloc_req, free_req, free_id,
        input  alloc_id, alloc_valid, alloc_empty
    );
    modport allocator (
        input  alloc_req, free_req, free_id,
        output alloc_id, alloc_valid, alloc_empty
    );
endinterface

// ─────────────────────────────────────────────
// TUE 请求 Interface（ctrl_plane → TUE）
// ─────────────────────────────────────────────
interface tue_req_if (input logic clk, input logic rst_n);
    logic                      valid;
    tue_req_t                  req;
    logic                      ready;
    logic                      done;    // 更新完成脉冲
    logic                      error;

    modport master (
        output valid, req,
        input  ready, done, error
    );
    modport slave (
        input  valid, req,
        output ready, done, error
    );
endinterface

// ─────────────────────────────────────────────
// TUE → MAU 配置 Interface（每级独立）
// ─────────────────────────────────────────────
interface mau_cfg_if (input logic clk, input logic rst_n);
    // TCAM 写
    logic                      tcam_wr_en;
    logic [10:0]               tcam_wr_addr;   // 2K entries
    logic [MAU_TCAM_KEY_W-1:0] tcam_wr_key;
    logic [MAU_TCAM_KEY_W-1:0] tcam_wr_mask;
    logic [15:0]               tcam_action_id;
    logic [15:0]               tcam_action_ptr;
    logic                      tcam_wr_valid;  // 条目有效位
    // Action SRAM 写
    logic                      asram_wr_en;
    logic [15:0]               asram_wr_addr;  // 64K entries
    logic [MAU_ASRAM_WIDTH-1:0] asram_wr_data;

    modport driver (
        output tcam_wr_en, tcam_wr_addr, tcam_wr_key, tcam_wr_mask,
               tcam_action_id, tcam_action_ptr, tcam_wr_valid,
               asram_wr_en, asram_wr_addr, asram_wr_data
    );
    modport receiver (
        input  tcam_wr_en, tcam_wr_addr, tcam_wr_key, tcam_wr_mask,
               tcam_action_id, tcam_action_ptr, tcam_wr_valid,
               asram_wr_en, asram_wr_addr, asram_wr_data
    );
endinterface

// ─────────────────────────────────────────────
// MAC RX cell 流 Interface（SerDes → Parser）
// ─────────────────────────────────────────────
interface mac_rx_if (input logic clk, input logic rst_n);
    logic [4:0]   port;      // 源端口 0-31
    logic         valid;
    logic         sof;
    logic         eof;
    logic [6:0]   eop_len;   // EOF 时有效字节数（1-64）
    logic [511:0] data;      // 64B cell
    logic         ready;

    modport src (
        output port, valid, sof, eof, eop_len, data,
        input  ready
    );
    modport dst (
        input  port, valid, sof, eof, eop_len, data,
        output ready
    );
endinterface

// ─────────────────────────────────────────────
// MAC TX cell 流 Interface（Deparser → SerDes）
// ─────────────────────────────────────────────
interface mac_tx_if (input logic clk, input logic rst_n);
    logic [4:0]   port;
    logic         valid;
    logic         sof;
    logic         eof;
    logic [6:0]   eop_len;
    logic [511:0] data;
    logic         ready;

    modport src (
        output port, valid, sof, eof, eop_len, data,
        input  ready
    );
    modport dst (
        input  port, valid, sof, eof, eop_len, data,
        output ready
    );
endinterface

`endif // RV_P4_IF_SV
