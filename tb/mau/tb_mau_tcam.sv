// tb_mau_tcam.sv
// MAU TCAM 单元测试
// 验证：插入/查找/删除/优先级/miss

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"

module tb_mau_tcam;
    import rv_p4_pkg::*;

    // ── 时钟复位 ──────────────────────────────
    logic clk = 0, rst_n = 0;
    always #0.3125 clk = ~clk;  // 1.6GHz

    // ── DUT 端口 ──────────────────────────────
    logic [MAU_TCAM_KEY_W-1:0] key;
    logic                       lookup_en;
    logic [10:0]                hit_idx;
    logic                       hit;
    logic [15:0]                action_id;
    logic [15:0]                action_ptr;

    logic                       wr_en;
    logic [10:0]                wr_addr;
    logic [MAU_TCAM_KEY_W-1:0]  wr_key;
    logic [MAU_TCAM_KEY_W-1:0]  wr_mask;
    logic [15:0]                wr_action_id;
    logic [15:0]                wr_action_ptr;
    logic                       wr_valid;

    mau_tcam dut (.*);

    // ── 任务：写入一条 TCAM 条目 ──────────────
    task write_entry(
        input int          idx,
        input logic [511:0] k,
        input logic [511:0] m,
        input logic [15:0]  aid,
        input logic [15:0]  aptr,
        input logic         vld
    );
        @(posedge clk);
        wr_en         = 1;
        wr_addr       = 11'(idx);
        wr_key        = k;
        wr_mask       = m;
        wr_action_id  = aid;
        wr_action_ptr = aptr;
        wr_valid      = vld;
        @(posedge clk);
        wr_en = 0;
    endtask

    // ── 任务：查找并检查结果 ──────────────────
    task do_lookup(
        input  logic [511:0] k,
        input  logic         exp_hit,
        input  logic [15:0]  exp_aid,
        input  string        msg
    );
        @(posedge clk);
        key       = k;
        lookup_en = 1;
        @(posedge clk);
        lookup_en = 0;
        @(posedge clk); // 等待输出寄存
        if (hit !== exp_hit) begin
            $display("FAIL [%s] hit=%b expected=%b", msg, hit, exp_hit);
            $finish;
        end
        if (exp_hit && action_id !== exp_aid) begin
            $display("FAIL [%s] action_id=%h expected=%h", msg, action_id, exp_aid);
            $finish;
        end
        $display("PASS [%s] hit=%b action_id=%h", msg, hit, action_id);
    endtask

    // ── 测试主体 ──────────────────────────────
    initial begin
        $dumpfile("tb_mau_tcam.vcd");
        $dumpvars(0, tb_mau_tcam);

        wr_en = 0; lookup_en = 0; key = '0;
        #5 rst_n = 1;

        // ── TC1：精确匹配 ──────────────────────
        // 写入条目 0：key=0xDEAD...，mask=全0（精确），action=0x1001
        write_entry(0,
            512'hDEADBEEF << 480,
            512'h0,
            16'h1001, 16'h0010, 1'b1);

        do_lookup(512'hDEADBEEF << 480, 1'b1, 16'h1001, "TC1 exact hit");
        do_lookup(512'hDEADBEEE << 480, 1'b0, 16'h0,    "TC1 exact miss");

        // ── TC2：通配符匹配（mask bit=1 → don't care）──
        // 写入条目 1：key=0xAA00，mask=0x00FF（低8b don't care）
        write_entry(1,
            512'hAA00,
            512'h00FF,
            16'h2001, 16'h0020, 1'b1);

        do_lookup(512'hAA42, 1'b1, 16'h2001, "TC2 wildcard hit");
        do_lookup(512'hBB42, 1'b0, 16'h0,    "TC2 wildcard miss");

        // ── TC3：优先级（低索引优先）──────────
        // 条目 0 和条目 2 都能匹配同一 key
        write_entry(2,
            512'hDEADBEEF << 480,
            512'h0,
            16'h3001, 16'h0030, 1'b1);

        // 应命中条目 0（索引更低）
        do_lookup(512'hDEADBEEF << 480, 1'b1, 16'h1001, "TC3 priority");

        // ── TC4：删除条目（valid=0）────────────
        write_entry(0,
            512'h0, 512'h0, 16'h0, 16'h0, 1'b0);

        // 现在应命中条目 2
        do_lookup(512'hDEADBEEF << 480, 1'b1, 16'h3001, "TC4 after delete");

        // ── TC5：全 miss ───────────────────────
        do_lookup(512'hCAFEBABE, 1'b0, 16'h0, "TC5 all miss");

        $display("\n=== All TCAM tests PASSED ===");
        $finish;
    end

    // 超时保护
    initial begin
        #50000;
        $display("TIMEOUT");
        $finish;
    end

endmodule
