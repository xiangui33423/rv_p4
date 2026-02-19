// ctrl_plane_xs.sv
// Control plane: real XSTop instantiation + AXI4-to-APB bridge
// RV-P4 Switch ASIC

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module ctrl_plane_xs
    import rv_p4_pkg::*;
(
    input  logic clk_ctrl,
    input  logic rst_ctrl_n,
    input  logic clk_cpu,
    input  logic rst_cpu_n,

    // PCIe (firmware load / management)
    input  logic         pcie_clk,
    input  logic [255:0] pcie_rx_data,
    output logic [255:0] pcie_tx_data,
    input  logic         pcie_rx_valid,
    output logic         pcie_tx_valid,

    // APB master (drives all slaves)
    apb_if.master apb [16],

    // TUE request interface
    tue_req_if.master tue_req,

    // JTAG
    input  logic tck, tms, tdi,
    output logic tdo
);

// ─────────────────────────────────────────────────────────────
// peripheral_0 AXI4 wires (XSTop master → APB bridge)
// ─────────────────────────────────────────────────────────────
logic        p_awready, p_awvalid;
logic [1:0]  p_awid;
logic [30:0] p_awaddr;
logic [7:0]  p_awlen;
logic [2:0]  p_awsize;
logic [1:0]  p_awburst;
logic        p_awlock;
logic [3:0]  p_awcache;
logic [2:0]  p_awprot;
logic [3:0]  p_awqos;
logic        p_wready, p_wvalid;
logic [63:0] p_wdata;
logic [7:0]  p_wstrb;
logic        p_wlast;
logic        p_bready, p_bvalid;
logic [1:0]  p_bid;
logic [1:0]  p_bresp;
logic        p_arready, p_arvalid;
logic [1:0]  p_arid;
logic [30:0] p_araddr;
logic [7:0]  p_arlen;
logic [2:0]  p_arsize;
logic [1:0]  p_arburst;
logic        p_arlock;
logic [3:0]  p_arcache;
logic [2:0]  p_arprot;
logic [3:0]  p_arqos;
logic        p_rready, p_rvalid;
logic [1:0]  p_rid;
logic [63:0] p_rdata;
logic [1:0]  p_rresp;
logic        p_rlast;

// ─────────────────────────────────────────────────────────────
// memory_0 AXI4 wires (XSTop master → tied off / SLVERR)
// ─────────────────────────────────────────────────────────────
logic         m_awready, m_awvalid;
logic [13:0]  m_awid;
logic [35:0]  m_awaddr;
logic [7:0]   m_awlen;
logic [2:0]   m_awsize;
logic [1:0]   m_awburst;
logic         m_awlock;
logic [3:0]   m_awcache;
logic [2:0]   m_awprot;
logic [3:0]   m_awqos;
logic         m_wready, m_wvalid;
logic [255:0] m_wdata;
logic [31:0]  m_wstrb;
logic         m_wlast;
logic         m_bready, m_bvalid;
logic [13:0]  m_bid;
logic [1:0]   m_bresp;
logic         m_arready, m_arvalid;
logic [13:0]  m_arid;
logic [35:0]  m_araddr;
logic [7:0]   m_arlen;
logic [2:0]   m_arsize;
logic [1:0]   m_arburst;
logic         m_arlock;
logic [3:0]   m_arcache;
logic [2:0]   m_arprot;
logic [3:0]   m_arqos;
logic         m_rready, m_rvalid;
logic [13:0]  m_rid;
logic [255:0] m_rdata;
logic [1:0]   m_rresp;
logic         m_rlast;

// ─────────────────────────────────────────────────────────────
// dma_0 AXI4 wires (XSTop slave → tied off)
// ─────────────────────────────────────────────────────────────
logic         d_awready, d_awvalid;
logic [13:0]  d_awid;
logic [35:0]  d_awaddr;
logic [7:0]   d_awlen;
logic [2:0]   d_awsize;
logic [1:0]   d_awburst;
logic         d_awlock;
logic [3:0]   d_awcache;
logic [2:0]   d_awprot;
logic [3:0]   d_awqos;
logic         d_wready, d_wvalid;
logic [255:0] d_wdata;
logic [31:0]  d_wstrb;
logic         d_wlast;
logic         d_bready, d_bvalid;
logic [13:0]  d_bid;
logic [1:0]   d_bresp;
logic         d_arready, d_arvalid;
logic [13:0]  d_arid;
logic [35:0]  d_araddr;
logic [7:0]   d_arlen;
logic [2:0]   d_arsize;
logic [1:0]   d_arburst;
logic         d_arlock;
logic [3:0]   d_arcache;
logic [2:0]   d_arprot;
logic [3:0]   d_arqos;
logic         d_rready, d_rvalid;
logic [13:0]  d_rid;
logic [255:0] d_rdata;
logic [1:0]   d_rresp;
logic         d_rlast;

// ─────────────────────────────────────────────────────────────
// XSTop instantiation
// ─────────────────────────────────────────────────────────────
XSTop u_xstop (
    // dma_0 slave (tied off - not used)
    .dma_0_awready          (d_awready),
    .dma_0_awvalid          (1'b0),
    .dma_0_awid             (14'b0),
    .dma_0_awaddr           (36'b0),
    .dma_0_awlen            (8'b0),
    .dma_0_awsize           (3'b0),
    .dma_0_awburst          (2'b0),
    .dma_0_awlock           (1'b0),
    .dma_0_awcache          (4'b0),
    .dma_0_awprot           (3'b0),
    .dma_0_awqos            (4'b0),
    .dma_0_wready           (d_wready),
    .dma_0_wvalid           (1'b0),
    .dma_0_wdata            (256'b0),
    .dma_0_wstrb            (32'b0),
    .dma_0_wlast            (1'b0),
    .dma_0_bready           (1'b1),
    .dma_0_bvalid           (d_bvalid),
    .dma_0_bid              (d_bid),
    .dma_0_bresp            (d_bresp),
    .dma_0_arready          (d_arready),
    .dma_0_arvalid          (1'b0),
    .dma_0_arid             (14'b0),
    .dma_0_araddr           (36'b0),
    .dma_0_arlen            (8'b0),
    .dma_0_arsize           (3'b0),
    .dma_0_arburst          (2'b0),
    .dma_0_arlock           (1'b0),
    .dma_0_arcache          (4'b0),
    .dma_0_arprot           (3'b0),
    .dma_0_arqos            (4'b0),
    .dma_0_rready           (1'b1),
    .dma_0_rvalid           (d_rvalid),
    .dma_0_rid              (d_rid),
    .dma_0_rdata            (d_rdata),
    .dma_0_rresp            (d_rresp),
    .dma_0_rlast            (d_rlast),
    // peripheral_0 master (→ AXI4-to-APB bridge)
    .peripheral_0_awready   (p_awready),
    .peripheral_0_awvalid   (p_awvalid),
    .peripheral_0_awid      (p_awid),
    .peripheral_0_awaddr    (p_awaddr),
    .peripheral_0_awlen     (p_awlen),
    .peripheral_0_awsize    (p_awsize),
    .peripheral_0_awburst   (p_awburst),
    .peripheral_0_awlock    (p_awlock),
    .peripheral_0_awcache   (p_awcache),
    .peripheral_0_awprot    (p_awprot),
    .peripheral_0_awqos     (p_awqos),
    .peripheral_0_wready    (p_wready),
    .peripheral_0_wvalid    (p_wvalid),
    .peripheral_0_wdata     (p_wdata),
    .peripheral_0_wstrb     (p_wstrb),
    .peripheral_0_wlast     (p_wlast),
    .peripheral_0_bready    (p_bready),
    .peripheral_0_bvalid    (p_bvalid),
    .peripheral_0_bid       (p_bid),
    .peripheral_0_bresp     (p_bresp),
    .peripheral_0_arready   (p_arready),
    .peripheral_0_arvalid   (p_arvalid),
    .peripheral_0_arid      (p_arid),
    .peripheral_0_araddr    (p_araddr),
    .peripheral_0_arlen     (p_arlen),
    .peripheral_0_arsize    (p_arsize),
    .peripheral_0_arburst   (p_arburst),
    .peripheral_0_arlock    (p_arlock),
    .peripheral_0_arcache   (p_arcache),
    .peripheral_0_arprot    (p_arprot),
    .peripheral_0_arqos     (p_arqos),
    .peripheral_0_rready    (p_rready),
    .peripheral_0_rvalid    (p_rvalid),
    .peripheral_0_rid       (p_rid),
    .peripheral_0_rdata     (p_rdata),
    .peripheral_0_rresp     (p_rresp),
    .peripheral_0_rlast     (p_rlast),
    // memory_0 master (tied off - no DDR, return SLVERR)
    .memory_0_awready       (m_awready),
    .memory_0_awvalid       (m_awvalid),
    .memory_0_awid          (m_awid),
    .memory_0_awaddr        (m_awaddr),
    .memory_0_awlen         (m_awlen),
    .memory_0_awsize        (m_awsize),
    .memory_0_awburst       (m_awburst),
    .memory_0_awlock        (m_awlock),
    .memory_0_awcache       (m_awcache),
    .memory_0_awprot        (m_awprot),
    .memory_0_awqos         (m_awqos),
    .memory_0_wready        (m_wready),
    .memory_0_wvalid        (m_wvalid),
    .memory_0_wdata         (m_wdata),
    .memory_0_wstrb         (m_wstrb),
    .memory_0_wlast         (m_wlast),
    .memory_0_bready        (m_bready),
    .memory_0_bvalid        (m_bvalid),
    .memory_0_bid           (m_bid),
    .memory_0_bresp         (m_bresp),
    .memory_0_arready       (m_arready),
    .memory_0_arvalid       (m_arvalid),
    .memory_0_arid          (m_arid),
    .memory_0_araddr        (m_araddr),
    .memory_0_arlen         (m_arlen),
    .memory_0_arsize        (m_arsize),
    .memory_0_arburst       (m_arburst),
    .memory_0_arlock        (m_arlock),
    .memory_0_arcache       (m_arcache),
    .memory_0_arprot        (m_arprot),
    .memory_0_arqos         (m_arqos),
    .memory_0_rready        (m_rready),
    .memory_0_rvalid        (m_rvalid),
    .memory_0_rid           (m_rid),
    .memory_0_rdata         (m_rdata),
    .memory_0_rresp         (m_rresp),
    .memory_0_rlast         (m_rlast),
    // clock / reset
    .io_clock               (clk_cpu),
    .io_reset               (~rst_cpu_n),
    .io_sram_config         (16'b0),
    .io_extIntrs            (64'b0),
    .io_pll0_lock           (1'b1),
    .io_pll0_ctrl_0         (),
    .io_pll0_ctrl_1         (),
    .io_pll0_ctrl_2         (),
    .io_pll0_ctrl_3         (),
    .io_pll0_ctrl_4         (),
    .io_pll0_ctrl_5         (),
    // JTAG
    .io_systemjtag_jtag_TCK       (tck),
    .io_systemjtag_jtag_TMS       (tms),
    .io_systemjtag_jtag_TDI       (tdi),
    .io_systemjtag_jtag_TDO_data  (tdo),
    .io_systemjtag_jtag_TDO_driven(),
    .io_systemjtag_reset          (~rst_cpu_n),
    .io_systemjtag_mfr_id         (11'h537),
    .io_systemjtag_part_number    (16'h0),
    .io_systemjtag_version        (4'h0),
    .io_debug_reset               (),
    // cacheable check: req tied off
    .io_cacheable_check_req_0_valid      (1'b0),
    .io_cacheable_check_req_0_bits_addr  (36'b0),
    .io_cacheable_check_req_0_bits_size  (2'b0),
    .io_cacheable_check_req_0_bits_cmd   (3'b0),
    .io_cacheable_check_req_1_valid      (1'b0),
    .io_cacheable_check_req_1_bits_addr  (36'b0),
    .io_cacheable_check_req_1_bits_size  (2'b0),
    .io_cacheable_check_req_1_bits_cmd   (3'b0),
    .io_cacheable_check_resp_0_ld        (),
    .io_cacheable_check_resp_0_st        (),
    .io_cacheable_check_resp_0_instr     (),
    .io_cacheable_check_resp_0_mmio      (),
    .io_cacheable_check_resp_1_ld        (),
    .io_cacheable_check_resp_1_st        (),
    .io_cacheable_check_resp_1_instr     (),
    .io_cacheable_check_resp_1_mmio      (),
    .io_riscv_halt_0                     ()
);

// ─────────────────────────────────────────────────────────────
// memory_0 tie-off: accept all transactions, return SLVERR
// ─────────────────────────────────────────────────────────────
// Write path: accept AW+W immediately, respond SLVERR
typedef enum logic [1:0] {
    MEM_IDLE   = 2'd0,
    MEM_WDATA  = 2'd1,
    MEM_BRESP  = 2'd2,
    MEM_RRESP  = 2'd3
} mem_st_t;

mem_st_t mem_wst, mem_rst;
logic [13:0] mem_wid_r, mem_rid_r;

always_ff @(posedge clk_cpu or negedge rst_cpu_n) begin
    if (!rst_cpu_n) begin
        mem_wst    <= MEM_IDLE;
        mem_wid_r  <= '0;
        m_awready  <= 1'b0;
        m_wready   <= 1'b0;
        m_bvalid   <= 1'b0;
        m_bid      <= '0;
        m_bresp    <= 2'b10; // SLVERR
    end else begin
        case (mem_wst)
            MEM_IDLE: begin
                m_awready <= 1'b1;
                m_wready  <= 1'b0;
                m_bvalid  <= 1'b0;
                if (m_awvalid && m_awready) begin
                    mem_wid_r <= m_awid;
                    m_awready <= 1'b0;
                    m_wready  <= 1'b1;
                    mem_wst   <= MEM_WDATA;
                end
            end
            MEM_WDATA: begin
                if (m_wvalid && m_wready && m_wlast) begin
                    m_wready <= 1'b0;
                    m_bvalid <= 1'b1;
                    m_bid    <= mem_wid_r;
                    m_bresp  <= 2'b10;
                    mem_wst  <= MEM_BRESP;
                end
            end
            MEM_BRESP: begin
                if (m_bready && m_bvalid) begin
                    m_bvalid <= 1'b0;
                    mem_wst  <= MEM_IDLE;
                end
            end
            default: mem_wst <= MEM_IDLE;
        endcase
    end
end

// Read path: accept AR immediately, return SLVERR
always_ff @(posedge clk_cpu or negedge rst_cpu_n) begin
    if (!rst_cpu_n) begin
        mem_rst   <= MEM_IDLE;
        mem_rid_r <= '0;
        m_arready <= 1'b0;
        m_rvalid  <= 1'b0;
        m_rid     <= '0;
        m_rdata   <= '0;
        m_rresp   <= 2'b10;
        m_rlast   <= 1'b0;
    end else begin
        case (mem_rst)
            MEM_IDLE: begin
                m_arready <= 1'b1;
                m_rvalid  <= 1'b0;
                if (m_arvalid && m_arready) begin
                    mem_rid_r <= m_arid;
                    m_arready <= 1'b0;
                    m_rvalid  <= 1'b1;
                    m_rid     <= m_arid;
                    m_rdata   <= '0;
                    m_rresp   <= 2'b10;
                    m_rlast   <= 1'b1;
                    mem_rst   <= MEM_RRESP;
                end
            end
            MEM_RRESP: begin
                if (m_rready && m_rvalid) begin
                    m_rvalid <= 1'b0;
                    m_rlast  <= 1'b0;
                    mem_rst  <= MEM_IDLE;
                end
            end
            default: mem_rst <= MEM_IDLE;
        endcase
    end
end

// ─────────────────────────────────────────────────────────────
// AXI4-to-APB bridge for peripheral_0
// peripheral_0 is AXI4 master (CPU drives it), 31-bit addr, 64-bit data
// APB slaves are 32-bit; we use lower 32 bits of wdata for writes.
// Address decode: addr[15:12] selects APB slave index (0-15)
// ─────────────────────────────────────────────────────────────

typedef enum logic [2:0] {
    APB_IDLE       = 3'd0,
    APB_AW_RECV    = 3'd1,
    APB_W_RECV     = 3'd2,
    APB_AR_RECV    = 3'd3,
    APB_SETUP      = 3'd4,
    APB_ENABLE     = 3'd5,
    APB_B_RESP     = 3'd6,
    APB_R_RESP     = 3'd7
} apb_st_t;

apb_st_t apb_st;

logic        apb_wr;          // 1=write, 0=read
logic [30:0] apb_addr_r;
logic [31:0] apb_wdata_r;
logic [1:0]  apb_id_r;
logic [3:0]  apb_sel_idx;

assign apb_sel_idx = apb_addr_r[15:12];

// Internal APB bus signals driven to interface array via generate
logic        apb_psel_r;
logic        apb_penable_r;
logic        apb_pwrite_r;
logic [11:0] apb_paddr_r;
logic [31:0] apb_pwdata_r;
logic [31:0] apb_prdata_r;
logic        apb_pready_r;

// AXI4 handshake signals driven by bridge
assign p_awready = (apb_st == APB_IDLE);
assign p_wready  = (apb_st == APB_AW_RECV) || (apb_st == APB_W_RECV);
assign p_arready = (apb_st == APB_IDLE) && !p_awvalid;
assign p_rready  = 1'b1;

always_ff @(posedge clk_cpu or negedge rst_cpu_n) begin
    if (!rst_cpu_n) begin
        apb_st       <= APB_IDLE;
        apb_wr       <= 1'b0;
        apb_addr_r   <= '0;
        apb_wdata_r  <= '0;
        apb_id_r     <= '0;
        apb_psel_r   <= 1'b0;
        apb_penable_r<= 1'b0;
        apb_pwrite_r <= 1'b0;
        apb_paddr_r  <= '0;
        apb_pwdata_r <= '0;
        apb_prdata_r <= '0;
        apb_pready_r <= 1'b0;
        p_bvalid     <= 1'b0;
        p_bid        <= '0;
        p_bresp      <= 2'b00;
        p_rvalid     <= 1'b0;
        p_rid        <= '0;
        p_rdata      <= '0;
        p_rresp      <= 2'b00;
        p_rlast      <= 1'b0;
    end else begin
        case (apb_st)
            APB_IDLE: begin
                apb_psel_r    <= 1'b0;
                apb_penable_r <= 1'b0;
                p_bvalid      <= 1'b0;
                p_rvalid      <= 1'b0;
                // Write takes priority over read
                if (p_awvalid) begin
                    apb_addr_r <= p_awaddr;
                    apb_id_r   <= p_awid;
                    apb_wr     <= 1'b1;
                    apb_st     <= APB_AW_RECV;
                end else if (p_arvalid) begin
                    apb_addr_r <= p_araddr;
                    apb_id_r   <= p_arid;
                    apb_wr     <= 1'b0;
                    apb_st     <= APB_AR_RECV;
                end
            end
            APB_AW_RECV: begin
                // Wait for W channel
                if (p_wvalid) begin
                    apb_wdata_r <= p_wdata[31:0];
                    apb_st      <= APB_SETUP;
                end
            end
            APB_AR_RECV: begin
                // AR already captured, go straight to APB SETUP
                apb_st <= APB_SETUP;
            end
            APB_SETUP: begin
                apb_psel_r    <= 1'b1;
                apb_penable_r <= 1'b0;
                apb_pwrite_r  <= apb_wr;
                apb_paddr_r   <= apb_addr_r[11:0];
                apb_pwdata_r  <= apb_wdata_r;
                apb_st        <= APB_ENABLE;
            end
            APB_ENABLE: begin
                apb_penable_r <= 1'b1;
                if (apb_pready_r) begin
                    apb_psel_r    <= 1'b0;
                    apb_penable_r <= 1'b0;
                    if (apb_wr) begin
                        p_bvalid <= 1'b1;
                        p_bid    <= apb_id_r;
                        p_bresp  <= 2'b00;
                        apb_st   <= APB_B_RESP;
                    end else begin
                        p_rvalid <= 1'b1;
                        p_rid    <= apb_id_r;
                        p_rdata  <= {32'b0, apb_prdata_r};
                        p_rresp  <= 2'b00;
                        p_rlast  <= 1'b1;
                        apb_st   <= APB_R_RESP;
                    end
                end
            end
            APB_B_RESP: begin
                if (p_bready && p_bvalid) begin
                    p_bvalid <= 1'b0;
                    apb_st   <= APB_IDLE;
                end
            end
            APB_R_RESP: begin
                if (p_rready && p_rvalid) begin
                    p_rvalid <= 1'b0;
                    p_rlast  <= 1'b0;
                    apb_st   <= APB_IDLE;
                end
            end
            default: apb_st <= APB_IDLE;
        endcase
    end
end

// Capture pready/prdata from selected slave.
// Use generate to build per-slave wires (constant indices required),
// then mux with a priority encoder in always_comb.
logic [15:0] apb_pready_vec;
logic [31:0] apb_prdata_vec [16];

generate
    genvar gj;
    for (gj = 0; gj < 16; gj++) begin : g_apb_rd
        assign apb_pready_vec[gj]  = apb[gj].pready;
        assign apb_prdata_vec[gj]  = apb[gj].prdata;
    end
endgenerate

always_comb begin
    apb_pready_r = 1'b0;
    apb_prdata_r = 32'b0;
    for (int i = 0; i < 16; i++) begin
        if (apb_sel_idx == 4'(i)) begin
            apb_pready_r = apb_pready_vec[i];
            apb_prdata_r = apb_prdata_vec[i];
        end
    end
end

// ─────────────────────────────────────────────────────────────
// Drive APB slave interfaces via generate (Verilator-compatible)
// No dynamic interface array indexing
// ─────────────────────────────────────────────────────────────
generate
    genvar gi;
    for (gi = 0; gi < 16; gi++) begin : g_apb_slave
        always_comb begin
            apb[gi].psel    = apb_psel_r    && (apb_sel_idx == 4'(gi));
            apb[gi].penable = apb_penable_r && (apb_sel_idx == 4'(gi));
            apb[gi].pwrite  = apb_pwrite_r;
            apb[gi].paddr   = apb_paddr_r;
            apb[gi].pwdata  = apb_pwdata_r;
        end
    end
endgenerate

// ─────────────────────────────────────────────────────────────
// TUE register file (APB slave index 3, addr[15:12]==3)
// Accumulates writes into tue_req_t fields, fires on COMMIT
// ─────────────────────────────────────────────────────────────
// TUE register shadows
logic [1:0]                tue_op_r;
logic [4:0]                tue_stage_r;
logic [15:0]               tue_table_id_r;
logic [MAU_TCAM_KEY_W-1:0] tue_key_r;
logic [MAU_TCAM_KEY_W-1:0] tue_mask_r;
logic [15:0]               tue_action_id_r;
logic [111:0]              tue_action_params_r;

// APB write to TUE slave (index 3)
wire apb3_wr = apb[3].psel && apb[3].penable && apb[3].pwrite;

always_ff @(posedge clk_cpu or negedge rst_cpu_n) begin
    if (!rst_cpu_n) begin
        tue_op_r           <= '0;
        tue_stage_r        <= '0;
        tue_table_id_r     <= '0;
        tue_key_r          <= '0;
        tue_mask_r         <= '0;
        tue_action_id_r    <= '0;
        tue_action_params_r<= '0;
        tue_req.valid      <= 1'b0;
        tue_req.req        <= '0;
    end else begin
        // Default: deassert valid after one cycle
        if (tue_req.ready)
            tue_req.valid <= 1'b0;

        if (apb3_wr) begin
            case (apb[3].paddr)
                TUE_REG_CMD:      tue_op_r        <= apb[3].pwdata[1:0];
                TUE_REG_TABLE_ID: tue_table_id_r  <= apb[3].pwdata[15:0];
                TUE_REG_STAGE:    tue_stage_r     <= apb[3].pwdata[4:0];
                TUE_REG_KEY_0:    tue_key_r[31:0]    <= apb[3].pwdata;
                12'h014:          tue_key_r[63:32]   <= apb[3].pwdata;
                12'h018:          tue_key_r[95:64]   <= apb[3].pwdata;
                12'h01C:          tue_key_r[127:96]  <= apb[3].pwdata;
                12'h020:          tue_key_r[159:128] <= apb[3].pwdata;
                12'h024:          tue_key_r[191:160] <= apb[3].pwdata;
                12'h028:          tue_key_r[223:192] <= apb[3].pwdata;
                12'h02C:          tue_key_r[255:224] <= apb[3].pwdata;
                12'h030:          tue_key_r[287:256] <= apb[3].pwdata;
                12'h034:          tue_key_r[319:288] <= apb[3].pwdata;
                12'h038:          tue_key_r[351:320] <= apb[3].pwdata;
                12'h03C:          tue_key_r[383:352] <= apb[3].pwdata;
                12'h040:          tue_key_r[415:384] <= apb[3].pwdata;
                12'h044:          tue_key_r[447:416] <= apb[3].pwdata;
                12'h048:          tue_key_r[479:448] <= apb[3].pwdata;
                TUE_REG_KEY_15:   tue_key_r[511:480] <= apb[3].pwdata;
                TUE_REG_MASK_0:   tue_mask_r[31:0]   <= apb[3].pwdata;
                12'h054:          tue_mask_r[63:32]   <= apb[3].pwdata;
                12'h058:          tue_mask_r[95:64]   <= apb[3].pwdata;
                12'h05C:          tue_mask_r[127:96]  <= apb[3].pwdata;
                12'h060:          tue_mask_r[159:128] <= apb[3].pwdata;
                12'h064:          tue_mask_r[191:160] <= apb[3].pwdata;
                12'h068:          tue_mask_r[223:192] <= apb[3].pwdata;
                12'h06C:          tue_mask_r[255:224] <= apb[3].pwdata;
                12'h070:          tue_mask_r[287:256] <= apb[3].pwdata;
                12'h074:          tue_mask_r[319:288] <= apb[3].pwdata;
                12'h078:          tue_mask_r[351:320] <= apb[3].pwdata;
                12'h07C:          tue_mask_r[383:352] <= apb[3].pwdata;
                12'h080:          tue_mask_r[415:384] <= apb[3].pwdata;
                12'h084:          tue_mask_r[447:416] <= apb[3].pwdata;
                12'h088:          tue_mask_r[479:448] <= apb[3].pwdata;
                TUE_REG_MASK_15:  tue_mask_r[511:480] <= apb[3].pwdata;
                TUE_REG_ACTION_ID: tue_action_id_r    <= apb[3].pwdata[15:0];
                TUE_REG_ACTION_P0: tue_action_params_r[31:0]   <= apb[3].pwdata;
                TUE_REG_ACTION_P1: tue_action_params_r[63:32]  <= apb[3].pwdata;
                TUE_REG_ACTION_P2: tue_action_params_r[95:64]  <= apb[3].pwdata;
                TUE_REG_COMMIT: begin
                    // Fire TUE request
                    tue_req.valid          <= 1'b1;
                    tue_req.req.op         <= tue_op_t'(tue_op_r);
                    tue_req.req.stage      <= tue_stage_r;
                    tue_req.req.table_id   <= tue_table_id_r;
                    tue_req.req.key        <= tue_key_r;
                    tue_req.req.mask       <= tue_mask_r;
                    tue_req.req.action_id  <= tue_action_id_r;
                    tue_req.req.action_params <= tue_action_params_r;
                end
                default: ;
            endcase
        end
    end
end

// APB[3] read: return STATUS (always ready, no error)
// pready is driven by the slave; for TUE we drive it here via the interface
// (apb[3].pready is an output from the slave modport - driven externally)
// Since ctrl_plane_xs IS the master, we need a stub pready for apb[3].
// The TUE slave (tue.sv) drives apb[3].pready. We just ensure our bridge
// sees it correctly - already handled via apb_pready_r mux above.

// ─────────────────────────────────────────────────────────────
// PCIe stub (same as ctrl_plane.sv)
// ─────────────────────────────────────────────────────────────
always_ff @(posedge pcie_clk or negedge rst_ctrl_n) begin
    if (!rst_ctrl_n) begin
        pcie_tx_valid <= 1'b0;
        pcie_tx_data  <= '0;
    end else if (pcie_rx_valid) begin
        pcie_tx_valid <= 1'b1;
        pcie_tx_data  <= {224'b0, 32'hDEAD_BEEF};
    end else begin
        pcie_tx_valid <= 1'b0;
    end
end

endmodule
