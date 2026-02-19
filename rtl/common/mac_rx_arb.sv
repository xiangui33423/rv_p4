// mac_rx_arb.sv
// 32 端口 MAC RX 汇聚仲裁器
// 轮询（Round-Robin）仲裁，将 32 路 cell 流合并为单路输出
// 保证同一帧的 cell 连续输出（帧内不切换端口）

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module mac_rx_arb
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 32 端口 RX 输入
    input  logic [31:0]        rx_valid,
    input  logic [31:0]        rx_sof,
    input  logic [31:0]        rx_eof,
    input  logic [31:0][6:0]   rx_eop_len,
    input  logic [31:0][511:0] rx_data,
    output logic [31:0]        rx_ready,

    // 单路输出（到 Parser）
    mac_rx_if.src out
);

    // ── 状态 ──────────────────────────────────
    typedef enum logic [1:0] {
        S_IDLE,     // 等待仲裁
        S_GRANT,    // 已授权某端口，传输帧中
        S_EOF       // 帧结束，等待下一仲裁
    } state_t;

    state_t          state;
    logic [4:0]      grant_port;   // 当前授权端口
    logic [4:0]      rr_ptr;       // 轮询指针

    // ── 轮询仲裁：找下一个有效端口 ────────────
    logic [4:0] next_port;
    logic       any_valid;

    always_comb begin
        next_port = rr_ptr;
        any_valid = 1'b0;
        for (int i = 0; i < NUM_PORTS; i++) begin
            logic [4:0] idx = (rr_ptr + 5'(i)) % 5'(NUM_PORTS);
            if (rx_valid[idx] && rx_sof[idx] && !any_valid) begin
                next_port = idx;
                any_valid = 1'b1;
            end
        end
    end

    // ── 主状态机 ──────────────────────────────
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state      <= S_IDLE;
            grant_port <= '0;
            rr_ptr     <= '0;
        end else begin
            case (state)
                S_IDLE: begin
                    if (any_valid) begin
                        grant_port <= next_port;
                        state      <= S_GRANT;
                        rr_ptr     <= (next_port + 5'd1) % 5'(NUM_PORTS);
                    end
                end

                S_GRANT: begin
                    // 当前帧传输中，等待 EOF
                    if (out.valid && out.ready && out.eof)
                        state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

    // ── 输出多路选择 ──────────────────────────
    always_comb begin
        // 默认：无输出
        out.valid   = 1'b0;
        out.port    = '0;
        out.sof     = 1'b0;
        out.eof     = 1'b0;
        out.eop_len = '0;
        out.data    = '0;
        rx_ready    = '0;

        if (state == S_GRANT) begin
            out.valid          = rx_valid[grant_port];
            out.port           = grant_port;
            out.sof            = rx_sof[grant_port];
            out.eof            = rx_eof[grant_port];
            out.eop_len        = rx_eop_len[grant_port];
            out.data           = rx_data[grant_port];
            rx_ready[grant_port] = out.ready;
        end
    end

endmodule
