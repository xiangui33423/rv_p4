// mau_hash.sv
// MAU Hash 单元
// 支持 CRC32 / CRC16 / Jenkins（通过 CSR 选择）
// 输入：PHV 中选取的字段（最多 64B）→ 输出：32b hash

`include "rv_p4_pkg.sv"

module mau_hash
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 输入：从 PHV 提取的 hash key（最多 64B = 512b）
    input  logic [511:0] hash_key,
    input  logic [5:0]   hash_key_len,  // 有效字节数（1-64）
    input  logic         hash_en,

    // 算法选择（来自 CSR）
    // 2'b00 = CRC32, 2'b01 = CRC16, 2'b10 = Jenkins, 2'b11 = Identity
    input  logic [1:0]   hash_sel,

    // 输出
    output logic [31:0]  hash_result,
    output logic         hash_valid
);

    // ── CRC32（IEEE 802.3 多项式 0xEDB88320）────
    function automatic logic [31:0] crc32_byte(
        input logic [31:0] crc,
        input logic [7:0]  data
    );
        logic [31:0] c = crc ^ {24'b0, data};
        for (int i = 0; i < 8; i++)
            c = c[0] ? (c >> 1) ^ 32'hEDB88320 : c >> 1;
        return c;
    endfunction

    function automatic logic [31:0] crc32(input logic [511:0] data, input int len);
        logic [31:0] crc = 32'hFFFFFFFF;
        for (int i = 0; i < 64; i++) begin
            if (i < len)
                crc = crc32_byte(crc, data[i*8 +: 8]);
        end
        return ~crc;
    endfunction

    // ── CRC16（CCITT 多项式 0x1021）─────────────
    function automatic logic [15:0] crc16_byte(
        input logic [15:0] crc,
        input logic [7:0]  data
    );
        logic [15:0] c = crc ^ {data, 8'b0};
        for (int i = 0; i < 8; i++)
            c = c[15] ? (c << 1) ^ 16'h1021 : c << 1;
        return c;
    endfunction

    function automatic logic [15:0] crc16(input logic [511:0] data, input int len);
        logic [15:0] crc = 16'hFFFF;
        for (int i = 0; i < 64; i++) begin
            if (i < len)
                crc = crc16_byte(crc, data[i*8 +: 8]);
        end
        return crc;
    endfunction

    // ── Jenkins one-at-a-time hash ───────────────
    function automatic logic [31:0] jenkins(input logic [511:0] data, input int len);
        logic [31:0] h = 32'b0;
        for (int i = 0; i < 64; i++) begin
            if (i < len) begin
                h = h + {24'b0, data[i*8 +: 8]};
                h = h + (h << 10);
                h = h ^ (h >> 6);
            end
        end
        h = h + (h << 3);
        h = h ^ (h >> 11);
        h = h + (h << 15);
        return h;
    endfunction

    // ── 计算并寄存 ────────────────────────────
    logic [31:0] result_comb;

    always_comb begin
        case (hash_sel)
            2'b00: result_comb = crc32(hash_key, int'(hash_key_len));
            2'b01: result_comb = {16'b0, crc16(hash_key, int'(hash_key_len))};
            2'b10: result_comb = jenkins(hash_key, int'(hash_key_len));
            2'b11: result_comb = hash_key[31:0]; // Identity（取低 32b）
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hash_result <= '0;
            hash_valid  <= 1'b0;
        end else begin
            hash_result <= result_comb;
            hash_valid  <= hash_en;
        end
    end

endmodule
