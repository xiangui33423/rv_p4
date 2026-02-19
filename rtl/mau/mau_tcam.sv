`timescale 1ns/1ps
// mau_tcam.sv
// MAU 级 TCAM（2K × 512b key+mask）
// 优先编码，最低索引优先，1 cycle 流水延迟

`include "rv_p4_pkg.sv"

module mau_tcam
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 查找
    input  logic [MAU_TCAM_KEY_W-1:0] key,
    input  logic                       lookup_en,
    output logic [10:0]                hit_idx,    // 命中条目索引
    output logic                       hit,        // 命中标志
    output logic [15:0]                action_id,
    output logic [15:0]                action_ptr,

    // 写配置（来自 mau_cfg_if）
    input  logic                       wr_en,
    input  logic [10:0]                wr_addr,
    input  logic [MAU_TCAM_KEY_W-1:0]  wr_key,
    input  logic [MAU_TCAM_KEY_W-1:0]  wr_mask,
    input  logic [15:0]                wr_action_id,
    input  logic [15:0]                wr_action_ptr,
    input  logic                       wr_valid
);

    localparam int DEPTH = MAU_TCAM_DEPTH; // 2048

    // TCAM 存储
    logic [MAU_TCAM_KEY_W-1:0] t_key  [DEPTH];
    logic [MAU_TCAM_KEY_W-1:0] t_mask [DEPTH];
    logic [15:0]                t_action_id  [DEPTH];
    logic [15:0]                t_action_ptr [DEPTH];
    logic                       t_valid      [DEPTH];

    // 写端口
    always_ff @(posedge clk) begin
        if (wr_en) begin
            t_key[wr_addr]        <= wr_key;
            t_mask[wr_addr]       <= wr_mask;
            t_action_id[wr_addr]  <= wr_action_id;
            t_action_ptr[wr_addr] <= wr_action_ptr;
            t_valid[wr_addr]      <= wr_valid;
        end
    end

    // 并行匹配（组合逻辑）
    // TCAM 语义：(key XOR t_key) AND (NOT t_mask) == 0
    // t_mask bit=1 → don't care；bit=0 → must match
    logic [DEPTH-1:0] match;
    always_comb begin
        for (int i = 0; i < DEPTH; i++)
            match[i] = t_valid[i] &&
                       (((key ^ t_key[i]) & ~t_mask[i]) == '0);
    end

    // 优先编码（最低索引优先）
    logic [10:0] pri_idx;
    logic        any_match;
    always_comb begin
        pri_idx   = '0;
        any_match = 1'b0;
        for (int i = DEPTH-1; i >= 0; i--) begin
            if (match[i]) begin
                pri_idx   = 11'(i);
                any_match = 1'b1;
            end
        end
    end

    // 输出寄存（1 cycle 延迟）
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hit        <= 1'b0;
            hit_idx    <= '0;
            action_id  <= '0;
            action_ptr <= '0;
        end else if (lookup_en) begin
            hit        <= any_match;
            hit_idx    <= pri_idx;
            action_id  <= any_match ? t_action_id[pri_idx]  : '0;
            action_ptr <= any_match ? t_action_ptr[pri_idx] : '0;
        end
    end

endmodule
