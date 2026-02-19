// mau_alu.sv
// MAU 动作 ALU
// 支持：字段赋值、加减、位操作、条件赋值、哈希写回

`include "rv_p4_pkg.sv"

module mau_alu
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 输入 PHV
    input  logic [PHV_BITS-1:0] phv_in,
    input  phv_meta_t           meta_in,
    input  logic                valid_in,

    // Action 参数（来自 Action SRAM）
    input  logic [15:0]         action_id,
    input  logic [111:0]        action_params,
    input  logic                action_valid,

    // Hash 结果（来自 hash 单元）
    input  logic [31:0]         hash_result,

    // 输出 PHV（修改后）
    output logic [PHV_BITS-1:0] phv_out,
    output phv_meta_t           meta_out,
    output logic                valid_out
);

    // ── Action ID 编码（与 C-to-HW 编译器约定）──
    // action_id[15:12] = 操作类型
    // action_id[11:8]  = 目标字段组
    // action_id[7:0]   = 子操作码
    localparam logic [3:0] OP_NOP       = 4'h0;
    localparam logic [3:0] OP_SET       = 4'h1;  // phv[dst] = value
    localparam logic [3:0] OP_COPY      = 4'h2;  // phv[dst] = phv[src]
    localparam logic [3:0] OP_ADD       = 4'h3;  // phv[dst] += value
    localparam logic [3:0] OP_SUB       = 4'h4;  // phv[dst] -= value
    localparam logic [3:0] OP_AND       = 4'h5;
    localparam logic [3:0] OP_OR        = 4'h6;
    localparam logic [3:0] OP_XOR       = 4'h7;
    localparam logic [3:0] OP_SET_META  = 4'h8;  // 修改 meta 字段
    localparam logic [3:0] OP_DROP      = 4'h9;  // meta.drop = 1
    localparam logic [3:0] OP_SET_PORT  = 4'hA;  // meta.eg_port = value
    localparam logic [3:0] OP_SET_PRIO  = 4'hB;  // meta.qos_prio = value
    localparam logic [3:0] OP_HASH_SET  = 4'hC;  // phv[dst] = hash_result
    localparam logic [3:0] OP_COND_SET  = 4'hD;  // if phv[cond] then set

    // action_params 位域解析
    // [111:80] dst_offset  (32b → 实际用低 10b，PHV 字节偏移)
    // [79:48]  src_offset  (32b → 实际用低 10b)
    // [47:16]  imm_value   (32b 立即数)
    // [15:8]   field_width (8b，操作字段宽度，字节数 1/2/4/6/8)
    // [7:0]    reserved

    logic [3:0]  op_type;
    logic [9:0]  dst_off;
    logic [9:0]  src_off;
    logic [31:0] imm_val;
    logic [7:0]  fwidth;

    assign op_type = action_id[15:12];
    assign dst_off = action_params[111:102];
    assign src_off = action_params[79:70];
    assign imm_val = action_params[47:16];
    assign fwidth  = action_params[15:8];

    // ── 组合 ALU 逻辑 ─────────────────────────
    logic [PHV_BITS-1:0] phv_modified;
    phv_meta_t           meta_modified;

    always_comb begin
        phv_modified = phv_in;
        meta_modified = meta_in;

        if (action_valid) begin
            case (op_type)
                OP_SET: begin
                    // 按字段宽度写立即数到 PHV
                    case (fwidth)
                        8'd1: phv_modified[dst_off*8 +:  8] = imm_val[7:0];
                        8'd2: phv_modified[dst_off*8 +: 16] = imm_val[15:0];
                        8'd4: phv_modified[dst_off*8 +: 32] = imm_val;
                        8'd6: phv_modified[dst_off*8 +: 48] = {16'b0, imm_val};
                        default: phv_modified[dst_off*8 +: 32] = imm_val;
                    endcase
                end

                OP_COPY: begin
                    case (fwidth)
                        8'd1: phv_modified[dst_off*8 +:  8] = phv_in[src_off*8 +:  8];
                        8'd2: phv_modified[dst_off*8 +: 16] = phv_in[src_off*8 +: 16];
                        8'd4: phv_modified[dst_off*8 +: 32] = phv_in[src_off*8 +: 32];
                        8'd6: phv_modified[dst_off*8 +: 48] = phv_in[src_off*8 +: 48];
                        default: phv_modified[dst_off*8 +: 32] = phv_in[src_off*8 +: 32];
                    endcase
                end

                OP_ADD: begin
                    case (fwidth)
                        8'd1: phv_modified[dst_off*8 +:  8] = phv_in[dst_off*8 +:  8] + imm_val[7:0];
                        8'd2: phv_modified[dst_off*8 +: 16] = phv_in[dst_off*8 +: 16] + imm_val[15:0];
                        8'd4: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] + imm_val;
                        default: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] + imm_val;
                    endcase
                end

                OP_SUB: begin
                    case (fwidth)
                        8'd1: phv_modified[dst_off*8 +:  8] = phv_in[dst_off*8 +:  8] - imm_val[7:0];
                        8'd2: phv_modified[dst_off*8 +: 16] = phv_in[dst_off*8 +: 16] - imm_val[15:0];
                        8'd4: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] - imm_val;
                        default: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] - imm_val;
                    endcase
                end

                OP_AND: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] & imm_val;
                OP_OR:  phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] | imm_val;
                OP_XOR: phv_modified[dst_off*8 +: 32] = phv_in[dst_off*8 +: 32] ^ imm_val;

                OP_DROP:     meta_modified.drop     = 1'b1;
                OP_SET_PORT: meta_modified.eg_port  = imm_val[4:0];
                OP_SET_PRIO: meta_modified.qos_prio = imm_val[2:0];

                OP_HASH_SET: begin
                    case (fwidth)
                        8'd2: phv_modified[dst_off*8 +: 16] = hash_result[15:0];
                        8'd4: phv_modified[dst_off*8 +: 32] = hash_result;
                        default: phv_modified[dst_off*8 +: 32] = hash_result;
                    endcase
                end

                OP_COND_SET: begin
                    // 条件赋值：若 phv[src_off] != 0 则执行 SET
                    if (phv_in[src_off*8 +: 8] != 8'b0)
                        phv_modified[dst_off*8 +: 32] = imm_val;
                end

                default: ; // NOP
            endcase
        end
    end

    // ── 输出寄存 ──────────────────────────────
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            phv_out   <= '0;
            meta_out  <= '0;
            valid_out <= 1'b0;
        end else begin
            phv_out   <= phv_modified;
            meta_out  <= meta_modified;
            valid_out <= valid_in;
        end
    end

endmodule
