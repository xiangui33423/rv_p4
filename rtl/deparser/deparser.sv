// deparser.sv
// Deparser RTL module — RV-P4 Switch ASIC
// Reconstructs Ethernet/IPv4 headers from PHV, reads packet cells from
// pkt_buffer, and drives per-port TX streams.

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module deparser
    import rv_p4_pkg::*;
(
    input  logic clk_dp,
    input  logic rst_dp_n,

    // PHV from TM
    phv_if.dst   phv_in,

    // Packet-buffer read port
    pb_rd_if.master pb_rd,

    // Per-port TX streams (32 ports)
    output logic [31:0]        tx_valid,
    output logic [31:0]        tx_sof,
    output logic [31:0]        tx_eof,
    output logic [31:0][6:0]   tx_eop_len,
    output logic [31:0][511:0] tx_data,
    input  logic [31:0]        tx_ready,

    // APB CSR slave
    apb_if.slave csr
);

// ─────────────────────────────────────────────
// Per-port descriptor FIFO
// ─────────────────────────────────────────────
localparam int Q_DEPTH = 8;
localparam int Q_PTR_W = 3;   // log2(8)

typedef struct packed {
    logic [CELL_ID_W-1:0] cell_id;
    logic [13:0]          pkt_len;
    logic [PHV_BITS-1:0]  phv;
    logic                 drop;
} qdesc_t;

qdesc_t             q_mem  [NUM_PORTS][Q_DEPTH];
logic [Q_PTR_W-1:0] q_head [NUM_PORTS];
logic [Q_PTR_W-1:0] q_tail [NUM_PORTS];
logic [Q_PTR_W:0]   q_cnt  [NUM_PORTS];

// ─────────────────────────────────────────────
// Enqueue side
// ─────────────────────────────────────────────
logic [4:0]  enq_port;
qdesc_t      enq_desc;
logic        enq_en;

assign enq_port     = phv_in.meta.eg_port;
assign enq_desc     = '{
    cell_id : phv_in.meta.cell_id,
    pkt_len : phv_in.meta.pkt_len,
    phv     : phv_in.data,
    drop    : phv_in.meta.drop
};
assign phv_in.ready = (q_cnt[enq_port] < (Q_PTR_W+1)'(Q_DEPTH));
assign enq_en       = phv_in.valid && phv_in.ready;

// ─────────────────────────────────────────────
// TX state machine per port
// ─────────────────────────────────────────────
typedef enum logic [2:0] {
    TX_IDLE,
    TX_HDR,
    TX_READ,
    TX_SEND,
    TX_FREE
} tx_state_t;

tx_state_t            tx_state  [NUM_PORTS];
logic [CELL_ID_W-1:0] tx_cell   [NUM_PORTS];
logic [13:0]          tx_pktlen [NUM_PORTS];
logic [PHV_BITS-1:0]  tx_phv    [NUM_PORTS];
logic                 tx_is_sof [NUM_PORTS];

// ─────────────────────────────────────────────
// Per-port generate: sequential FIFO + state machine
// ─────────────────────────────────────────────
generate
    for (genvar p = 0; p < NUM_PORTS; p++) begin : gen_port

        always_ff @(posedge clk_dp or negedge rst_dp_n) begin
            if (!rst_dp_n) begin
                q_head[p]    <= '0;
                q_tail[p]    <= '0;
                q_cnt[p]     <= '0;
                tx_state[p]  <= TX_IDLE;
                tx_cell[p]   <= '0;
                tx_pktlen[p] <= '0;
                tx_phv[p]    <= '0;
                tx_is_sof[p] <= 1'b1;
            end else begin

                // Enqueue
                if (enq_en && enq_port == 5'(p)) begin
                    q_mem[p][q_tail[p]] <= enq_desc;
                    q_tail[p]           <= q_tail[p] + 1'b1;
                    q_cnt[p]            <= q_cnt[p]  + 1'b1;
                end

                case (tx_state[p])
                    TX_IDLE: begin
                        if (q_cnt[p] > 0) begin
                            tx_cell[p]   <= q_mem[p][q_head[p]].cell_id;
                            tx_pktlen[p] <= q_mem[p][q_head[p]].pkt_len;
                            tx_phv[p]    <= q_mem[p][q_head[p]].phv;
                            q_head[p]    <= q_head[p] + 1'b1;
                            q_cnt[p]     <= q_cnt[p]  - 1'b1;
                            if (q_mem[p][q_head[p]].drop)
                                tx_state[p] <= TX_IDLE;
                            else
                                tx_state[p] <= TX_HDR;
                        end
                    end
                    TX_HDR: begin
                        tx_is_sof[p] <= 1'b1;
                        tx_state[p]  <= TX_READ;
                    end
                    TX_READ: begin
                        if (pb_rd.req_ready)
                            tx_state[p] <= TX_SEND;
                    end
                    TX_SEND: begin
                        if (tx_ready[p] && pb_rd.rsp_valid) begin
                            tx_is_sof[p] <= 1'b0;
                            if (pb_rd.rsp_eof)
                                tx_state[p] <= TX_FREE;
                            else
                                tx_cell[p] <= pb_rd.rsp_next_cell_id;
                        end
                    end
                    TX_FREE: begin
                        tx_state[p] <= TX_IDLE;
                    end
                    default: tx_state[p] <= TX_IDLE;
                endcase
            end
        end

    end
endgenerate

// ─────────────────────────────────────────────
// Packet-buffer read request arbitration
// ─────────────────────────────────────────────
always_comb begin
    pb_rd.req_valid   = 1'b0;
    pb_rd.req_cell_id = '0;
    for (int p = 0; p < NUM_PORTS; p++) begin
        if (!pb_rd.req_valid &&
            (tx_state[p] == TX_READ || tx_state[p] == TX_SEND)) begin
            pb_rd.req_valid   = 1'b1;
            pb_rd.req_cell_id = tx_cell[p];
        end
    end
end

// ─────────────────────────────────────────────
// Per-port header reconstruction intermediate signals
// (module-level to satisfy Verilator no-local-in-always_comb)
// ─────────────────────────────────────────────
logic [511:0] port_cell   [NUM_PORTS];
logic [15:0]  port_etype  [NUM_PORTS];
logic [15:0]  port_old_ttl_word [NUM_PORTS];
logic [15:0]  port_new_ttl_word [NUM_PORTS];
logic [15:0]  port_old_csum     [NUM_PORTS];
logic [16:0]  port_tmp          [NUM_PORTS];
logic [15:0]  port_new_csum     [NUM_PORTS];

// ─────────────────────────────────────────────
// Per-port combinatorial header overlay (generate)
// ─────────────────────────────────────────────
generate
    for (genvar p = 0; p < NUM_PORTS; p++) begin : gen_hdr

        always_comb begin
            // Start from raw cell data
            port_cell[p] = pb_rd.rsp_data;

            // EtherType from PHV bytes 12-13
            port_etype[p] = {tx_phv[p][PHV_BITS-1-12*8 -: 8],
                             tx_phv[p][PHV_BITS-1-13*8 -: 8]};

            // RFC-1624 checksum intermediates (computed regardless, used only for IPv4)
            port_old_ttl_word[p] = {tx_phv[p][PHV_BITS-1-26*8 -: 8],
                                    tx_phv[p][PHV_BITS-1-27*8 -: 8]};
            port_new_ttl_word[p] = {tx_phv[p][PHV_BITS-1-26*8 -: 8] - 8'd1,
                                    tx_phv[p][PHV_BITS-1-27*8 -: 8]};
            port_old_csum[p]     = {tx_phv[p][PHV_BITS-1-24*8 -: 8],
                                    tx_phv[p][PHV_BITS-1-25*8 -: 8]};
            // ~old_csum + ~old_word
            port_tmp[p]      = {1'b0, ~port_old_csum[p]} +
                               {1'b0, ~port_old_ttl_word[p]};
            // fold carry
            port_tmp[p]      = {1'b0, port_tmp[p][15:0] + {15'b0, port_tmp[p][16]}} +
                               {1'b0, port_new_ttl_word[p]};
            port_new_csum[p] = ~(port_tmp[p][15:0] + {15'b0, port_tmp[p][16]});

            if (tx_is_sof[p]) begin
                // dst MAC bytes 0-5
                port_cell[p][511:504] = tx_phv[p][PHV_BITS-1-0*8  -: 8];
                port_cell[p][503:496] = tx_phv[p][PHV_BITS-1-1*8  -: 8];
                port_cell[p][495:488] = tx_phv[p][PHV_BITS-1-2*8  -: 8];
                port_cell[p][487:480] = tx_phv[p][PHV_BITS-1-3*8  -: 8];
                port_cell[p][479:472] = tx_phv[p][PHV_BITS-1-4*8  -: 8];
                port_cell[p][471:464] = tx_phv[p][PHV_BITS-1-5*8  -: 8];
                // src MAC bytes 6-11
                port_cell[p][463:456] = tx_phv[p][PHV_BITS-1-6*8  -: 8];
                port_cell[p][455:448] = tx_phv[p][PHV_BITS-1-7*8  -: 8];
                port_cell[p][447:440] = tx_phv[p][PHV_BITS-1-8*8  -: 8];
                port_cell[p][439:432] = tx_phv[p][PHV_BITS-1-9*8  -: 8];
                port_cell[p][431:424] = tx_phv[p][PHV_BITS-1-10*8 -: 8];
                port_cell[p][423:416] = tx_phv[p][PHV_BITS-1-11*8 -: 8];
                // EtherType bytes 12-13
                port_cell[p][415:408] = tx_phv[p][PHV_BITS-1-12*8 -: 8];
                port_cell[p][407:400] = tx_phv[p][PHV_BITS-1-13*8 -: 8];

                if (port_etype[p] == ETYPE_IPV4) begin
                    // IPv4 header bytes 14-23 copied as-is from PHV
                    port_cell[p][399:392] = tx_phv[p][PHV_BITS-1-14*8 -: 8];
                    port_cell[p][391:384] = tx_phv[p][PHV_BITS-1-15*8 -: 8];
                    port_cell[p][383:376] = tx_phv[p][PHV_BITS-1-16*8 -: 8];
                    port_cell[p][375:368] = tx_phv[p][PHV_BITS-1-17*8 -: 8];
                    port_cell[p][367:360] = tx_phv[p][PHV_BITS-1-18*8 -: 8];
                    port_cell[p][359:352] = tx_phv[p][PHV_BITS-1-19*8 -: 8];
                    port_cell[p][351:344] = tx_phv[p][PHV_BITS-1-20*8 -: 8];
                    port_cell[p][343:336] = tx_phv[p][PHV_BITS-1-21*8 -: 8];
                    port_cell[p][335:328] = tx_phv[p][PHV_BITS-1-22*8 -: 8];
                    port_cell[p][327:320] = tx_phv[p][PHV_BITS-1-23*8 -: 8];
                    // checksum bytes 24-25 (updated)
                    port_cell[p][319:312] = port_new_csum[p][15:8];
                    port_cell[p][311:304] = port_new_csum[p][7:0];
                    // TTL byte 26 (decremented), proto byte 27
                    port_cell[p][303:296] = tx_phv[p][PHV_BITS-1-26*8 -: 8] - 8'd1;
                    port_cell[p][295:288] = tx_phv[p][PHV_BITS-1-27*8 -: 8];
                    // src IP bytes 28-31 (from PHV bytes 30-33)
                    port_cell[p][287:280] = tx_phv[p][PHV_BITS-1-30*8 -: 8];
                    port_cell[p][279:272] = tx_phv[p][PHV_BITS-1-31*8 -: 8];
                    port_cell[p][271:264] = tx_phv[p][PHV_BITS-1-32*8 -: 8];
                    port_cell[p][263:256] = tx_phv[p][PHV_BITS-1-33*8 -: 8];
                    // dst IP bytes 32-35 (from PHV bytes 34-37)
                    port_cell[p][255:248] = tx_phv[p][PHV_BITS-1-34*8 -: 8];
                    port_cell[p][247:240] = tx_phv[p][PHV_BITS-1-35*8 -: 8];
                    port_cell[p][239:232] = tx_phv[p][PHV_BITS-1-36*8 -: 8];
                    port_cell[p][231:224] = tx_phv[p][PHV_BITS-1-37*8 -: 8];
                end
            end
        end

    end
endgenerate

// ─────────────────────────────────────────────
// TX output mux
// ─────────────────────────────────────────────
always_comb begin
    tx_valid   = '0;
    tx_sof     = '0;
    tx_eof     = '0;
    tx_eop_len = '0;
    for (int p = 0; p < NUM_PORTS; p++) tx_data[p] = '0;

    for (int p = 0; p < NUM_PORTS; p++) begin
        if (tx_state[p] == TX_SEND && pb_rd.rsp_valid) begin
            tx_valid[p]   = 1'b1;
            tx_sof[p]     = tx_is_sof[p];
            tx_eof[p]     = pb_rd.rsp_eof;
            tx_eop_len[p] = pb_rd.rsp_eof ? tx_pktlen[p][6:0] : 7'd64;
            tx_data[p]    = port_cell[p];
        end
    end
end

// ─────────────────────────────────────────────
// CSR stub
// ─────────────────────────────────────────────
assign csr.prdata  = '0;
assign csr.pready  = 1'b1;
assign csr.pslverr = 1'b0;

endmodule
