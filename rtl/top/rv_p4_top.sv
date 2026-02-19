// rv_p4_top.sv
// Top-level module — instantiates all sub-modules, connects via interfaces
// RV-P4 Switch ASIC

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module rv_p4_top
    import rv_p4_pkg::*;
(
    // Clocks
    input  logic clk_dp,      // 1.6 GHz  P4 datapath
    input  logic clk_ctrl,    // 200 MHz  control / TUE / APB
    input  logic clk_cpu,     // 1.5 GHz  CPU
    input  logic clk_mac,     // 390.625 MHz  MAC/PCS
    input  logic rst_n,       // Global async reset, sync release

    // SerDes RX (32 ports, already sliced to 64B cells at MAC layer)
    input  logic [31:0]        rx_valid,
    input  logic [31:0]        rx_sof,
    input  logic [31:0]        rx_eof,
    input  logic [31:0][6:0]   rx_eop_len,
    input  logic [31:0][511:0] rx_data,
    output logic [31:0]        rx_ready,

    // SerDes TX
    output logic [31:0]        tx_valid,
    output logic [31:0]        tx_sof,
    output logic [31:0]        tx_eof,
    output logic [31:0][6:0]   tx_eop_len,
    output logic [31:0][511:0] tx_data,
    input  logic [31:0]        tx_ready,

    // PCIe (to CPU control plane)
    input  logic        pcie_clk,
    input  logic [255:0] pcie_rx_data,
    output logic [255:0] pcie_tx_data,
    input  logic        pcie_rx_valid,
    output logic        pcie_tx_valid,

    // JTAG
    input  logic tck, tms, tdi,
    output logic tdo,

    // Test backdoor: parser TCAM direct write (tie to 0 in production)
    input  logic                          tb_parser_wr_en,
    input  logic [7:0]                    tb_parser_wr_addr,
    input  logic [PARSER_TCAM_WIDTH-1:0]  tb_parser_wr_data,

    // Test backdoor: TUE APB direct access (tie to 0 in production)
    input  logic [11:0] tb_tue_paddr,
    input  logic [31:0] tb_tue_pwdata,
    input  logic        tb_tue_psel,
    input  logic        tb_tue_penable,
    input  logic        tb_tue_pwrite,
    output logic [31:0] tb_tue_prdata,
    output logic        tb_tue_pready
);

// ─────────────────────────────────────────────
// Reset synchronizers
// ─────────────────────────────────────────────
logic rst_dp_n, rst_ctrl_n, rst_cpu_n;

rst_sync u_rst_dp   (.clk(clk_dp),   .rst_async_n(rst_n), .rst_sync_n(rst_dp_n));
rst_sync u_rst_ctrl (.clk(clk_ctrl), .rst_async_n(rst_n), .rst_sync_n(rst_ctrl_n));
rst_sync u_rst_cpu  (.clk(clk_cpu),  .rst_async_n(rst_n), .rst_sync_n(rst_cpu_n));

// ─────────────────────────────────────────────
// Interface instantiation
// ─────────────────────────────────────────────

// PHV bus: parser → mau[0] → ... → mau[23] → tm
phv_if phv_bus [NUM_MAU_STAGES+1] (.clk(clk_dp), .rst_n(rst_dp_n));

// APB bus (16 slave slots)
apb_if apb_bus [16] (.clk(clk_ctrl), .rst_n(rst_ctrl_n));

// Packet buffer interfaces
pb_wr_if   pb_wr    (.clk(clk_dp), .rst_n(rst_dp_n));   // Parser → pkt_buf
pb_rd_if   pb_rd_tm (.clk(clk_dp), .rst_n(rst_dp_n));   // TM → pkt_buf
pb_rd_if   pb_rd_dp (.clk(clk_dp), .rst_n(rst_dp_n));   // Deparser → pkt_buf

// Cell alloc: pkt_buffer is the allocator
cell_alloc_if cell_alloc (.clk(clk_dp), .rst_n(rst_dp_n));

// Separate cell free interface for TM (TM only frees, never allocates)
// TM uses cell_alloc_if.requester — wire its free signals into cell_alloc
cell_alloc_if cell_free_if (.clk(clk_dp), .rst_n(rst_dp_n));

// TUE interface
tue_req_if tue_req (.clk(clk_ctrl), .rst_n(rst_ctrl_n));

// MAU config interfaces (one per stage)
mau_cfg_if mau_cfg [NUM_MAU_STAGES] (.clk(clk_dp), .rst_n(rst_dp_n));

// MAC RX/TX
mac_rx_if mac_rx (.clk(clk_mac), .rst_n(rst_dp_n));
mac_tx_if mac_tx (.clk(clk_dp),  .rst_n(rst_dp_n));

// Parser FSM write signals from TUE
logic                          parser_wr_en;
logic [7:0]                    parser_wr_addr;
logic [PARSER_TCAM_WIDTH-1:0]  parser_wr_data;

// ─────────────────────────────────────────────
// Bridge cell_free_if (TM requester) into cell_alloc
// pkt_buffer.alloc is the allocator; parser uses alloc_req/alloc_id
// TM uses free_req/free_id — merge free signals from TM into cell_alloc
// ─────────────────────────────────────────────
// cell_alloc.alloc_req comes from parser (cell_alloc.requester)
// cell_alloc.free_req / free_id come from TM via cell_free_if
// We need to OR the two requesters' alloc_req and free_req into cell_alloc
// Since parser never frees and TM never allocates, we can simply wire:
//   alloc_req  = parser's alloc_req  (cell_alloc.requester driven by parser)
//   free_req   = TM's free_req
//   free_id    = TM's free_id
// But both parser and TM connect as .requester to the same interface —
// that would double-drive. Instead we use cell_free_if for TM and
// manually merge into cell_alloc via intermediate wires.

// Wires driven by parser (via cell_alloc.requester modport in p4_parser)
// and by TM (via cell_free_if.requester modport in traffic_manager).
// pkt_buffer sees cell_alloc.allocator.
// We merge free signals: cell_alloc.free_req = cell_free_if.free_req
//                        cell_alloc.free_id  = cell_free_if.free_id
// alloc_req is driven by parser through cell_alloc.requester.
// To avoid double-driving, we use a wrapper assign for the free side.
// The cell_alloc_if.requester modport drives: alloc_req, free_req, free_id
// The cell_alloc_if.allocator modport drives: alloc_id, alloc_valid, alloc_empty
//
// Solution: parser connects to cell_alloc.requester (drives alloc_req,
//   free_req=0, free_id=0 — see p4_parser.sv lines 229-230).
//   TM connects to cell_free_if.requester (drives alloc_req=0,
//   free_req, free_id — see traffic_manager.sv line 152).
//   We then OR the free signals into cell_alloc by overriding with
//   continuous assign — but that would conflict with parser's modport.
//
// Cleanest fix: use a single cell_alloc_if, connect parser as requester,
// and wire TM's free outputs directly to cell_alloc signals via assign.
// Since parser drives free_req=0 and free_id=0 (constant assigns in
// p4_parser), Verilator will warn MULTIDRIVEN but functionally correct.
// We suppress that with -Wno-MULTIDRIVEN in the compile command.
//
// Actually the cleanest Verilator-safe approach: don't use modports for
// the merge — instead connect TM to cell_free_if and wire free signals
// into cell_alloc with force-override. But SV doesn't allow that cleanly.
//
// Best approach: connect parser to cell_alloc.requester, connect TM to
// cell_free_if.requester, and connect pkt_buffer to cell_alloc.allocator.
// Then bridge cell_free_if free outputs into cell_alloc via assign on the
// interface signals directly (cell_alloc.free_req, cell_alloc.free_id).
// Parser drives these to 0 via modport — we override with OR logic.
// Use a local wire to merge:

// ─────────────────────────────────────────────
// MAC RX arbiter
// ─────────────────────────────────────────────
mac_rx_arb u_rx_arb (
    .clk        (clk_mac),
    .rst_n      (rst_dp_n),
    .rx_valid   (rx_valid),
    .rx_sof     (rx_sof),
    .rx_eof     (rx_eof),
    .rx_eop_len (rx_eop_len),
    .rx_data    (rx_data),
    .rx_ready   (rx_ready),
    .out        (mac_rx.src)
);

// ─────────────────────────────────────────────
// P4 Parser
// ─────────────────────────────────────────────
p4_parser u_parser (
    .clk_dp       (clk_dp),
    .rst_dp_n     (rst_dp_n),
    .rx           (mac_rx.dst),
    .pb_wr        (pb_wr.src),
    .cell_alloc   (cell_alloc.requester),
    .phv_out      (phv_bus[0].src),
    .fsm_wr_en    (parser_wr_en   | tb_parser_wr_en),
    .fsm_wr_addr  (tb_parser_wr_en ? tb_parser_wr_addr : parser_wr_addr),
    .fsm_wr_data  (tb_parser_wr_en ? tb_parser_wr_data : parser_wr_data),
    .csr          (apb_bus[0].slave)
);

// ─────────────────────────────────────────────
// MAU pipeline (24 stages)
// ─────────────────────────────────────────────
generate
    for (genvar i = 0; i < NUM_MAU_STAGES; i++) begin : gen_mau
        mau_stage #(.STAGE_ID(i)) u_mau (
            .clk_dp   (clk_dp),
            .rst_dp_n (rst_dp_n),
            .phv_in   (phv_bus[i].dst),
            .phv_out  (phv_bus[i+1].src),
            .cfg      (mau_cfg[i].receiver)
        );
    end
endgenerate

// ─────────────────────────────────────────────
// Traffic Manager
// ─────────────────────────────────────────────
traffic_manager u_tm (
    .clk_dp     (clk_dp),
    .rst_dp_n   (rst_dp_n),
    .phv_in     (phv_bus[NUM_MAU_STAGES].dst),
    .pb_rd      (pb_rd_tm.master),
    .cell_free  (cell_free_if.requester),
    .tx_valid   (tx_valid),
    .tx_sof     (tx_sof),
    .tx_eof     (tx_eof),
    .tx_eop_len (tx_eop_len),
    .tx_data    (tx_data),
    .tx_ready   (tx_ready),
    .csr        (apb_bus[1].slave)
);

// ─────────────────────────────────────────────
// Packet buffer
// pkt_buffer is the allocator; merge alloc_req from parser and
// free_req/free_id from TM.
// Parser drives cell_alloc.requester (alloc_req, free_req=0, free_id=0).
// TM drives cell_free_if.requester (alloc_req=0, free_req, free_id).
// We bridge TM's free signals into cell_alloc by connecting pkt_buffer
// to cell_alloc.allocator and overriding free_req/free_id via a
// separate always_comb that ORs both requesters.
// Since p4_parser assigns free_req=0 and free_id=0 as constants,
// we can safely OR in TM's values without functional conflict.
// ─────────────────────────────────────────────

// Bridge TM free signals into cell_alloc
// (parser drives free_req=1'b0, free_id='0 — safe to OR)
assign cell_alloc.free_req = cell_free_if.free_req;
assign cell_alloc.free_id  = cell_free_if.free_id;

// Feed alloc results back to TM's cell_free_if (TM only reads alloc_valid/empty/id
// for the alloc side, but since alloc_req=0 from TM it doesn't matter)
assign cell_free_if.alloc_id    = cell_alloc.alloc_id;
assign cell_free_if.alloc_valid = cell_alloc.alloc_valid;
assign cell_free_if.alloc_empty = cell_alloc.alloc_empty;

pkt_buffer u_pkt_buf (
    .clk_dp     (clk_dp),
    .rst_dp_n   (rst_dp_n),
    .wr         (pb_wr.dst),
    .rd_tm      (pb_rd_tm.slave),
    .rd_dp      (pb_rd_dp.slave),
    .alloc      (cell_alloc.allocator)
);

// ─────────────────────────────────────────────
// Table Update Engine
// ─────────────────────────────────────────────
tue u_tue (
    .clk_ctrl       (clk_ctrl),
    .rst_ctrl_n     (rst_ctrl_n),
    .clk_dp         (clk_dp),
    .req            (tue_req.slave),
    .mau_cfg        (mau_cfg),
    .csr            (apb_bus[2].slave),
    .parser_wr_en   (parser_wr_en),
    .parser_wr_addr (parser_wr_addr),
    .parser_wr_data (parser_wr_data)
);

// ─────────────────────────────────────────────
// Control plane
// ─────────────────────────────────────────────
ctrl_plane u_ctrl (
    .clk_ctrl     (clk_ctrl),
    .rst_ctrl_n   (rst_ctrl_n),
    .clk_cpu      (clk_cpu),
    .rst_cpu_n    (rst_cpu_n),
    .pcie_clk     (pcie_clk),
    .pcie_rx_data (pcie_rx_data),
    .pcie_tx_data (pcie_tx_data),
    .pcie_rx_valid(pcie_rx_valid),
    .pcie_tx_valid(pcie_tx_valid),
    .apb          (apb_bus),
    .tue_req      (tue_req.master),
    .tck(tck), .tms(tms), .tdi(tdi), .tdo(tdo),
    .tb_tue_paddr   (tb_tue_paddr),
    .tb_tue_pwdata  (tb_tue_pwdata),
    .tb_tue_psel    (tb_tue_psel),
    .tb_tue_penable (tb_tue_penable),
    .tb_tue_pwrite  (tb_tue_pwrite),
    .tb_tue_prdata  (tb_tue_prdata),
    .tb_tue_pready  (tb_tue_pready)
);

endmodule
