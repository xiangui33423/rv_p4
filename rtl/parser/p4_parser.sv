// p4_parser.sv
// P4 可编程解析器顶层
// 64 状态 FSM + TCAM 状态转移 + PHV 构建
// 并行将原始 cell 写入包缓冲

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module p4_parser
    import rv_p4_pkg::*;
(
    input  logic clk_dp,
    input  logic rst_dp_n,

    // MAC RX cell 流（来自 mac_rx_arb）
    mac_rx_if.dst    rx,

    // 包缓冲写入
    pb_wr_if.src     pb_wr,

    // Cell 分配
    cell_alloc_if.requester cell_alloc,

    // PHV 输出 → MAU[0]
    phv_if.src       phv_out,

    // Parser TCAM 直写接口（来自 TUE / 初始化）
    input  logic                          fsm_wr_en,
    input  logic [7:0]                    fsm_wr_addr,
    input  logic [PARSER_TCAM_WIDTH-1:0]  fsm_wr_data,

    // CSR（APB 从端）
    apb_if.slave     csr
);

    // ── 解析状态机 ────────────────────────────
    typedef enum logic [2:0] {
        PS_IDLE,        // 等待 SOF
        PS_LATCH,       // 锁存 cell，触发 TCAM 查找
        PS_TCAM_WAIT,   // 等待 TCAM 结果（1 cycle）
        PS_PROCESS,     // 处理 TCAM 结果，更新 PHV
        PS_PAYLOAD,     // 报头解析完成，继续接收 payload cell
        PS_EMIT,        // 输出 PHV
        PS_DROP         // 错误丢弃
    } parse_state_t;

    parse_state_t ps;

    // ── 锁存 cell ─────────────────────────────
    logic [511:0] cell_latch;   // 当前处理 cell 数据
    logic         cell_eof;     // 当前 cell 是否为帧末尾
    logic [6:0]   cell_eop_len;
    logic [4:0]   cell_port;

    // ── PHV 构建缓冲 ──────────────────────────
    logic [PHV_BITS-1:0] phv_buf;
    phv_meta_t           phv_meta_buf;

    // ── FSM 状态 ──────────────────────────────
    logic [5:0]  fsm_state;
    logic [13:0] hdr_ptr;       // 帧内报头字节偏移

    // ── TCAM 接口 ─────────────────────────────
    logic [63:0] byte_window;
    logic        tcam_lookup_valid;
    logic [5:0]  tcam_next_state;
    logic [7:0]  tcam_extract_offset;
    logic [7:0]  tcam_extract_len;
    logic [9:0]  tcam_phv_dst;
    logic [7:0]  tcam_hdr_advance;
    logic        tcam_hit;

    // ── Cell 链表 ─────────────────────────────
    logic [CELL_ID_W-1:0] first_cell_id;
    logic                  frame_active;

    // ── 字节窗口：从锁存 cell 取 8B ──────────
    always_comb begin
        automatic int off = int'(hdr_ptr[5:0]);
        byte_window = cell_latch[off*8 +: 64];
    end

    // ── TCAM 实例 ─────────────────────────────
    parser_tcam u_tcam (
        .clk            (clk_dp),
        .rst_n          (rst_dp_n),
        .lookup_state   (fsm_state),
        .lookup_window  (byte_window),
        .lookup_valid   (tcam_lookup_valid),
        .next_state     (tcam_next_state),
        .extract_offset (tcam_extract_offset),
        .extract_len    (tcam_extract_len),
        .phv_dst_offset (tcam_phv_dst),
        .hdr_advance    (tcam_hdr_advance),
        .hit            (tcam_hit),
        .lookup_ready   (),
        // 配置写口：直写接口优先，APB 次之
        .wr_en          (fsm_wr_en | (csr.psel & csr.penable & csr.pwrite
                         & (csr.paddr[11:8] == 4'hF))),
        .wr_addr        (fsm_wr_en ? fsm_wr_addr : csr.paddr[7:0]),
        .wr_data        (fsm_wr_en ? fsm_wr_data :
                         {csr.pwdata, {(PARSER_TCAM_WIDTH-32){1'b0}}})
    );

    // ── 主状态机 ──────────────────────────────
    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            ps           <= PS_IDLE;
            fsm_state    <= '0;
            hdr_ptr      <= '0;
            phv_buf      <= '0;
            phv_meta_buf <= '0;
            cell_latch   <= '0;
            cell_eof     <= 1'b0;
            cell_eop_len <= '0;
            cell_port    <= '0;
            frame_active <= 1'b0;
            first_cell_id<= '0;
        end else begin
            case (ps)

                // ── 等待帧起始 ──────────────────
                PS_IDLE: begin
                    if (rx.valid && rx.sof) begin
                        // 锁存 cell，进入解析
                        cell_latch   <= rx.data;
                        cell_eof     <= rx.eof;
                        cell_eop_len <= rx.eop_len;
                        cell_port    <= rx.port;
                        frame_active <= 1'b1;
                        first_cell_id<= cell_alloc.alloc_id;

                        phv_buf      <= '0;
                        phv_meta_buf <= '0;
                        phv_meta_buf.ig_port   <= rx.port;
                        phv_meta_buf.drop      <= 1'b0;
                        phv_meta_buf.multicast <= 1'b0;
                        phv_meta_buf.mirror    <= 1'b0;
                        phv_meta_buf.qos_prio  <= 3'd0;
                        phv_meta_buf.cell_id   <= cell_alloc.alloc_id;

                        fsm_state <= 6'd1;  // ST_ETHERNET
                        hdr_ptr   <= '0;
                        ps        <= PS_LATCH;
                    end
                end

                // ── 触发 TCAM 查找 ──────────────
                // tcam_lookup_valid 在此状态拉高（组合逻辑）
                // 下一拍进入 PS_TCAM_WAIT 等待结果
                PS_LATCH: begin
                    ps <= PS_TCAM_WAIT;
                end

                // ── 等待 TCAM 结果（1 cycle）────
                PS_TCAM_WAIT: begin
                    ps <= PS_PROCESS;
                end

                // ── 处理 TCAM 结果 ───────────────
                PS_PROCESS: begin
                    if (tcam_hit) begin
                        // 提取字段到 PHV（按 extract_len 字节）
                        // 简化：提取 1B 到 phv_buf[phv_dst]
                        phv_buf[tcam_phv_dst*8 +: 8] <=
                            cell_latch[tcam_extract_offset*8 +: 8];

                        hdr_ptr   <= hdr_ptr + 14'(tcam_hdr_advance);
                        fsm_state <= tcam_next_state;

                        if (tcam_next_state == 6'h3F) begin
                            // ACCEPT：报头解析完成
                            phv_meta_buf.pkt_len <= cell_eof
                                ? 14'(cell_eop_len)
                                : 14'd64;
                            ps <= cell_eof ? PS_EMIT : PS_PAYLOAD;
                        end else begin
                            // 继续解析下一字段
                            ps <= PS_LATCH;
                        end
                    end else begin
                        // TCAM miss → 丢弃
                        ps <= cell_eof ? PS_IDLE : PS_DROP;
                        phv_meta_buf.drop <= 1'b1;
                        frame_active      <= cell_eof ? 1'b0 : 1'b1;
                    end
                end

                // ── 接收 payload cell ────────────
                PS_PAYLOAD: begin
                    if (rx.valid && rx.ready) begin
                        phv_meta_buf.pkt_len <= phv_meta_buf.pkt_len
                            + (rx.eof ? 14'(rx.eop_len) : 14'd64);
                        if (rx.eof)
                            ps <= PS_EMIT;
                    end
                end

                // ── 输出 PHV ─────────────────────
                PS_EMIT: begin
                    if (phv_out.ready) begin
                        ps           <= PS_IDLE;
                        frame_active <= 1'b0;
                    end
                end

                // ── 丢弃帧 ───────────────────────
                PS_DROP: begin
                    if (rx.valid && rx.ready && rx.eof) begin
                        ps           <= PS_IDLE;
                        frame_active <= 1'b0;
                    end
                end

                default: ps <= PS_IDLE;
            endcase
        end
    end

    // ── TCAM 查找触发（PS_LATCH 时拉高）──────
    assign tcam_lookup_valid = (ps == PS_LATCH);

    // ── Cell 分配请求 ─────────────────────────
    // 在 PS_IDLE 收到 SOF 时分配首 cell；
    // 在 PS_PAYLOAD 收到非 EOF cell 时分配下一 cell
    assign cell_alloc.alloc_req = (ps == PS_IDLE && rx.valid && rx.sof) ||
                                  (ps == PS_PAYLOAD && rx.valid && rx.ready && !rx.eof);
    assign cell_alloc.free_req  = 1'b0;
    assign cell_alloc.free_id   = '0;

    // ── 包缓冲写入 ────────────────────────────
    // PS_IDLE 收到 SOF 时写首 cell（已锁存）
    // PS_PAYLOAD 收到 cell 时写入
    assign pb_wr.valid    = (ps == PS_IDLE && rx.valid && rx.sof) ||
                            (ps == PS_PAYLOAD && rx.valid && rx.ready);
    assign pb_wr.cell_id  = cell_alloc.alloc_id;
    assign pb_wr.data     = rx.data;
    assign pb_wr.sof      = rx.sof;
    assign pb_wr.eof      = rx.eof;
    assign pb_wr.data_len = rx.eof ? rx.eop_len : 7'd64;

    // ── RX 背压 ───────────────────────────────
    // 仅在 PS_IDLE 和 PS_PAYLOAD 接收新 cell
    assign rx.ready = (ps == PS_IDLE) || (ps == PS_PAYLOAD);

    // ── PHV 输出 ──────────────────────────────
    assign phv_out.valid = (ps == PS_EMIT);
    assign phv_out.data  = phv_buf;
    assign phv_out.meta  = phv_meta_buf;

    // ── CSR ───────────────────────────────────
    assign csr.prdata  = {28'b0, 1'b0, frame_active, (ps == PS_DROP), (ps == PS_EMIT)};
    assign csr.pready  = 1'b1;
    assign csr.pslverr = 1'b0;

endmodule
