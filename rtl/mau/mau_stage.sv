// mau_stage.sv
// MAU 单级顶层（Match-Action Unit）
// 内部 4 子级流水：crossbar → TCAM → Action SRAM → ALU
// 吞吐：1 PHV/cycle（全流水）

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module mau_stage
    import rv_p4_pkg::*;
#(
    parameter int STAGE_ID = 0
)(
    input  logic clk_dp,
    input  logic rst_dp_n,

    // PHV 输入（来自上一级或 Parser）
    phv_if.dst   phv_in,

    // PHV 输出（到下一级或 TM）
    phv_if.src   phv_out,

    // 配置接口（来自 TUE）
    mau_cfg_if.receiver cfg
);

    // ─────────────────────────────────────────
    // 子级 0：PHV crossbar — 提取 match key
    // ─────────────────────────────────────────
    // key_sel 由编译器烧录到 key_sel_sram（每级独立）
    // 简化：直接取 PHV 低 512b 作为 match key
    logic [MAU_TCAM_KEY_W-1:0] match_key;
    logic [PHV_BITS-1:0]       phv_s0;
    phv_meta_t                 meta_s0;
    logic                      valid_s0;

    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            valid_s0 <= 1'b0;
            phv_s0   <= '0;
            meta_s0  <= '0;
            match_key<= '0;
        end else if (phv_in.valid && phv_in.ready) begin
            valid_s0  <= 1'b1;
            phv_s0    <= phv_in.data;
            meta_s0   <= phv_in.meta;
            match_key <= phv_in.data[MAU_TCAM_KEY_W-1:0];
        end else begin
            valid_s0 <= 1'b0;
        end
    end

    assign phv_in.ready = phv_out.ready; // 背压透传

    // ─────────────────────────────────────────
    // 子级 1：TCAM 查找
    // ─────────────────────────────────────────
    logic [10:0]  tcam_hit_idx;
    logic         tcam_hit;
    logic [15:0]  tcam_action_id;
    logic [15:0]  tcam_action_ptr;

    logic [PHV_BITS-1:0] phv_s1;
    phv_meta_t           meta_s1;
    logic                valid_s1;

    mau_tcam u_tcam (
        .clk           (clk_dp),
        .rst_n         (rst_dp_n),
        .key           (match_key),
        .lookup_en     (valid_s0),
        .hit_idx       (tcam_hit_idx),
        .hit           (tcam_hit),
        .action_id     (tcam_action_id),
        .action_ptr    (tcam_action_ptr),
        // 配置写口
        .wr_en         (cfg.tcam_wr_en),
        .wr_addr       (cfg.tcam_wr_addr),
        .wr_key        (cfg.tcam_wr_key),
        .wr_mask       (cfg.tcam_wr_mask),
        .wr_action_id  (cfg.tcam_action_id),
        .wr_action_ptr (cfg.tcam_action_ptr),
        .wr_valid      (cfg.tcam_wr_valid)
    );

    // PHV/meta 随流水线延迟一拍（与 TCAM 对齐）
    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            valid_s1 <= 1'b0;
            phv_s1   <= '0;
            meta_s1  <= '0;
        end else begin
            valid_s1 <= valid_s0;
            phv_s1   <= phv_s0;
            meta_s1  <= meta_s0;
        end
    end

    // ─────────────────────────────────────────
    // 子级 2：Action SRAM 读取
    // ─────────────────────────────────────────
    // Action SRAM：64K × 128b（action_id + params）
    logic [MAU_ASRAM_WIDTH-1:0] asram [MAU_ASRAM_DEPTH];

    logic [15:0]         asram_action_id;
    logic [111:0]        asram_params;
    logic [PHV_BITS-1:0] phv_s2;
    phv_meta_t           meta_s2;
    logic                valid_s2;
    logic                hit_s2;

    // SRAM 写（来自 TUE）
    always_ff @(posedge clk_dp) begin
        if (cfg.asram_wr_en)
            asram[cfg.asram_wr_addr] <= cfg.asram_wr_data;
    end

    // SRAM 读（1 cycle）
    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            valid_s2      <= 1'b0;
            asram_action_id <= '0;
            asram_params  <= '0;
            phv_s2        <= '0;
            meta_s2       <= '0;
            hit_s2        <= 1'b0;
        end else begin
            valid_s2        <= valid_s1;
            hit_s2          <= tcam_hit;
            phv_s2          <= phv_s1;
            meta_s2         <= meta_s1;
            if (tcam_hit) begin
                asram_action_id <= asram[tcam_action_ptr][127:112];
                asram_params    <= asram[tcam_action_ptr][111:0];
            end else begin
                asram_action_id <= '0;
                asram_params    <= '0;
            end
        end
    end

    // ─────────────────────────────────────────
    // Hash 单元（与 TCAM 并行，结果在子级 2 可用）
    // ─────────────────────────────────────────
    logic [31:0] hash_result;
    logic        hash_valid;

    mau_hash u_hash (
        .clk         (clk_dp),
        .rst_n       (rst_dp_n),
        .hash_key    (match_key),
        .hash_key_len(6'd16),   // 默认取 16B（5-tuple），可由 CSR 配置
        .hash_en     (valid_s0),
        .hash_sel    (2'b00),   // CRC32，可由 CSR 配置
        .hash_result (hash_result),
        .hash_valid  (hash_valid)
    );

    // ─────────────────────────────────────────
    // 子级 3：ALU 执行 + PHV 写回
    // ─────────────────────────────────────────
    logic [PHV_BITS-1:0] phv_s3;
    phv_meta_t           meta_s3;
    logic                valid_s3;

    mau_alu u_alu (
        .clk          (clk_dp),
        .rst_n        (rst_dp_n),
        .phv_in       (phv_s2),
        .meta_in      (meta_s2),
        .valid_in     (valid_s2),
        .action_id    (asram_action_id),
        .action_params(asram_params),
        .action_valid (hit_s2),
        .hash_result  (hash_result),
        .phv_out      (phv_s3),
        .meta_out     (meta_s3),
        .valid_out    (valid_s3)
    );

    // ─────────────────────────────────────────
    // PHV 输出
    // ─────────────────────────────────────────
    assign phv_out.valid = valid_s3;
    assign phv_out.data  = phv_s3;
    assign phv_out.meta  = meta_s3;

endmodule
