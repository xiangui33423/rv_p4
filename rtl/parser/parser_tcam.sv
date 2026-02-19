// parser_tcam.sv
// Parser 状态转移 TCAM（256 × 640b）
// 输入：当前状态 + 字节窗口 → 输出：下一状态 + 提取控制

`include "rv_p4_pkg.sv"

module parser_tcam
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 查找接口
    input  logic [5:0]   lookup_state,   // 当前 FSM 状态（0-63）
    input  logic [63:0]  lookup_window,  // 当前字节窗口（8B）
    input  logic         lookup_valid,
    output logic [5:0]   next_state,     // 下一状态
    output logic [7:0]   extract_offset, // PHV 提取偏移（字节）
    output logic [7:0]   extract_len,    // 提取长度（字节）
    output logic [9:0]   phv_dst_offset, // PHV 目标偏移（字节）
    output logic [7:0]   hdr_advance,    // 报头指针推进字节数
    output logic         hit,            // 命中标志
    output logic         lookup_ready,

    // 写配置接口（来自 TUE / 初始化）
    input  logic         wr_en,
    input  logic [7:0]   wr_addr,        // 条目索引 0-255
    input  logic [PARSER_TCAM_WIDTH-1:0] wr_data  // 完整条目
);

    // ── TCAM 存储（综合时映射到 SRAM + 比较逻辑）────
    // 每条 640b 条目位域：
    //  [639:634] key_state   (6b)  当前状态
    //  [633:570] key_window  (64b) 字节窗口
    //  [569:506] mask_state  (6b + 58b padding)
    //  [505:442] mask_window (64b)
    //  [441:436] next_state  (6b)
    //  [435:428] extract_offset (8b)
    //  [427:420] extract_len    (8b)
    //  [419:410] phv_dst_offset (10b)
    //  [409:402] hdr_advance    (8b)
    //  [401]     valid
    //  [400:0]   reserved

    localparam int DEPTH = PARSER_TCAM_DEPTH; // 256

    logic [PARSER_TCAM_WIDTH-1:0] tcam_mem [DEPTH];
    logic                         tcam_valid [DEPTH];

    // 写端口
    always_ff @(posedge clk) begin
        if (wr_en) begin
            tcam_mem[wr_addr]   <= wr_data;
            tcam_valid[wr_addr] <= wr_data[401];
        end
    end

    // ── 优先编码查找（组合逻辑，最低索引优先）────
    logic [DEPTH-1:0] match;

    always_comb begin
        for (int i = 0; i < DEPTH; i++) begin
            logic [5:0]  k_state  = tcam_mem[i][639:634];
            logic [63:0] k_window = tcam_mem[i][633:570];
            logic [5:0]  m_state  = tcam_mem[i][569:564];
            logic [63:0] m_window = tcam_mem[i][505:442];

            logic state_match  = &((lookup_state  ~^ k_state)  | m_state);
            logic window_match = &((lookup_window ~^ k_window) | m_window);

            match[i] = tcam_valid[i] && state_match && window_match;
        end
    end

    // 优先编码：找最低命中索引
    logic [$clog2(DEPTH)-1:0] hit_idx;
    logic                      any_hit;

    always_comb begin
        hit_idx = '0;
        any_hit = 1'b0;
        for (int i = DEPTH-1; i >= 0; i--) begin
            if (match[i]) begin
                hit_idx = $clog2(DEPTH)'(i);
                any_hit = 1'b1;
            end
        end
    end

    // ── 输出寄存（1 cycle 延迟）─────────────────
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hit           <= 1'b0;
            next_state    <= '0;
            extract_offset<= '0;
            extract_len   <= '0;
            phv_dst_offset<= '0;
            hdr_advance   <= '0;
        end else if (lookup_valid) begin
            hit            <= any_hit;
            next_state     <= any_hit ? tcam_mem[hit_idx][441:436] : '0;
            extract_offset <= any_hit ? tcam_mem[hit_idx][435:428] : '0;
            extract_len    <= any_hit ? tcam_mem[hit_idx][427:420] : '0;
            phv_dst_offset <= any_hit ? tcam_mem[hit_idx][419:410] : '0;
            hdr_advance    <= any_hit ? tcam_mem[hit_idx][409:402] : '0;
        end
    end

    assign lookup_ready = 1'b1; // 单周期流水，始终就绪

endmodule
