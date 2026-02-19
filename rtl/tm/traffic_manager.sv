// traffic_manager.sv
// 流量管理器 + Deparser
// 每端口 1 个 FIFO（深度 8），简化调度

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module traffic_manager
    import rv_p4_pkg::*;
(
    input  logic clk_dp,
    input  logic rst_dp_n,

    phv_if.dst   phv_in,
    pb_rd_if.master pb_rd,
    cell_alloc_if.requester cell_free,

    output logic [31:0]        tx_valid,
    output logic [31:0]        tx_sof,
    output logic [31:0]        tx_eof,
    output logic [31:0][6:0]   tx_eop_len,
    output logic [31:0][511:0] tx_data,
    input  logic [31:0]        tx_ready,

    apb_if.slave csr
);

    // ── 每端口 FIFO（深度 8）─────────────────
    localparam int Q_DEPTH = 8;
    localparam int Q_PTR_W = 3;  // log2(8)

    typedef struct packed {
        logic [CELL_ID_W-1:0] cell_id;
        logic [13:0]          pkt_len;
        logic [2:0]           qos_prio;
        logic                 drop;
    } qdesc_t;

    qdesc_t              q_mem  [NUM_PORTS][Q_DEPTH];
    logic [Q_PTR_W-1:0]  q_head [NUM_PORTS];
    logic [Q_PTR_W-1:0]  q_tail [NUM_PORTS];
    logic [Q_PTR_W:0]    q_cnt  [NUM_PORTS];

    // ── 入队（使用 eg_port 直接索引端口 FIFO）─
    logic [4:0]  enq_port;
    qdesc_t      enq_desc;
    logic        enq_en;

    assign enq_port = phv_in.meta.eg_port;
    assign enq_desc = '{
        cell_id  : phv_in.meta.cell_id,
        pkt_len  : phv_in.meta.pkt_len,
        qos_prio : phv_in.meta.qos_prio,
        drop     : phv_in.meta.drop
    };
    assign enq_en       = phv_in.valid && phv_in.ready && !phv_in.meta.drop;
    assign phv_in.ready = (q_cnt[enq_port] < (Q_PTR_W+1)'(Q_DEPTH));

    // ── TX 状态机（每端口独立）───────────────
    typedef enum logic [1:0] {
        TX_IDLE,
        TX_READ,
        TX_SEND,
        TX_FREE
    } tx_state_t;

    tx_state_t           tx_state [NUM_PORTS];
    logic [CELL_ID_W-1:0] tx_cell [NUM_PORTS];

    // ── 每端口独立 always_ff（避免跨端口变量索引）
    generate
        for (genvar p = 0; p < NUM_PORTS; p++) begin : gen_port

            always_ff @(posedge clk_dp or negedge rst_dp_n) begin
                if (!rst_dp_n) begin
                    q_head[p]   <= '0;
                    q_tail[p]   <= '0;
                    q_cnt[p]    <= '0;
                    tx_state[p] <= TX_IDLE;
                    tx_cell[p]  <= '0;
                end else begin
                    // 入队
                    if (enq_en && enq_port == 5'(p)) begin
                        q_mem[p][q_tail[p]] <= enq_desc;
                        q_tail[p]           <= q_tail[p] + 1'b1;
                        q_cnt[p]            <= q_cnt[p]  + 1'b1;
                    end

                    // TX 状态机
                    case (tx_state[p])
                        TX_IDLE: begin
                            if (q_cnt[p] > 0) begin
                                tx_cell[p]  <= q_mem[p][q_head[p]].cell_id;
                                q_head[p]   <= q_head[p] + 1'b1;
                                q_cnt[p]    <= q_cnt[p]  - 1'b1;
                                tx_state[p] <= TX_READ;
                            end
                        end
                        TX_READ: begin
                            tx_state[p] <= TX_SEND;
                        end
                        TX_SEND: begin
                            if (tx_ready[p] && pb_rd.rsp_valid) begin
                                if (pb_rd.rsp_eof)
                                    tx_state[p] <= TX_FREE;
                                else
                                    tx_cell[p] <= pb_rd.rsp_next_cell_id;
                            end
                        end
                        TX_FREE: begin
                            tx_state[p] <= TX_IDLE;
                        end
                    endcase
                end
            end

        end
    endgenerate

    // ── 包缓冲读请求（优先级：端口 0 最高）───
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

    // ── TX 输出 ───────────────────────────────
    always_comb begin
        tx_valid   = '0;
        tx_sof     = '0;
        tx_eof     = '0;
        tx_eop_len = '0;
        for (int p = 0; p < NUM_PORTS; p++) tx_data[p] = '0;

        for (int p = 0; p < NUM_PORTS; p++) begin
            if (tx_state[p] == TX_SEND && pb_rd.rsp_valid) begin
                tx_valid[p]   = 1'b1;
                tx_data[p]    = pb_rd.rsp_data;
                tx_eof[p]     = pb_rd.rsp_eof;
                tx_eop_len[p] = 7'd64;
            end
        end
    end

    // ── Cell 释放（端口 0 简化）──────────────
    assign cell_free.alloc_req = 1'b0;
    assign cell_free.free_req  = (tx_state[0] == TX_FREE);
    assign cell_free.free_id   = tx_cell[0];

    // ── CSR ───────────────────────────────────
    assign csr.prdata  = '0;
    assign csr.pready  = 1'b1;
    assign csr.pslverr = 1'b0;

endmodule
