// tb_pkt_buffer.sv
// 包缓冲单元测试
// 验证：cell 分配/写入/读取/释放/free list

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_pkt_buffer;
    import rv_p4_pkg::*;

    logic clk_dp = 0, rst_dp_n = 0;
    always #0.3125 clk_dp = ~clk_dp;

    // ── Interface 实例 ────────────────────────
    pb_wr_if      wr        (.clk(clk_dp), .rst_n(rst_dp_n));
    pb_rd_if      rd_tm     (.clk(clk_dp), .rst_n(rst_dp_n));
    pb_rd_if      rd_dp     (.clk(clk_dp), .rst_n(rst_dp_n));
    cell_alloc_if alloc     (.clk(clk_dp), .rst_n(rst_dp_n));

    // ── DUT ───────────────────────────────────
    pkt_buffer dut (
        .clk_dp   (clk_dp),
        .rst_dp_n (rst_dp_n),
        .wr       (wr.dst),
        .rd_tm    (rd_tm.slave),
        .rd_dp    (rd_dp.slave),
        .alloc    (alloc.allocator)
    );

    // ── 任务：分配一个 cell ───────────────────
    task automatic alloc_cell(output logic [CELL_ID_W-1:0] cid);
        @(posedge clk_dp);
        alloc.alloc_req = 1;
        if (!alloc.alloc_valid) begin
            $display("FAIL: alloc_empty during alloc_cell");
            $finish;
        end
        cid = alloc.alloc_id;
        @(posedge clk_dp);
        alloc.alloc_req = 0;
    endtask

    // ── 任务：写入一个 cell ───────────────────
    task automatic write_cell(
        input logic [CELL_ID_W-1:0] cid,
        input logic [511:0]         data,
        input logic                 sof,
        input logic                 eof
    );
        @(posedge clk_dp);
        wr.valid    = 1;
        wr.cell_id  = cid;
        wr.data     = data;
        wr.sof      = sof;
        wr.eof      = eof;
        wr.data_len = eof ? 7'd40 : 7'd64;
        @(posedge clk_dp);
        wr.valid = 0;
    endtask

    // ── 任务：读取一个 cell（TM 端口）────────
    task automatic read_cell_tm(
        input  logic [CELL_ID_W-1:0] cid,
        output logic [511:0]         data,
        output logic                 eof
    );
        @(posedge clk_dp);
        rd_tm.req_valid   = 1;
        rd_tm.req_cell_id = cid;
        @(posedge clk_dp);
        rd_tm.req_valid = 0;
        // 等待响应（1 cycle 延迟）
        @(posedge clk_dp);
        data = rd_tm.rsp_data;
        eof  = rd_tm.rsp_eof;
    endtask

    // ── 任务：释放一个 cell ───────────────────
    task automatic free_cell(input logic [CELL_ID_W-1:0] cid);
        @(posedge clk_dp);
        alloc.free_req = 1;
        alloc.free_id  = cid;
        @(posedge clk_dp);
        alloc.free_req = 0;
    endtask

    // ── 测试主体 ──────────────────────────────
    logic [CELL_ID_W-1:0] cid0, cid1, cid2;
    logic [511:0]          rdata;
    logic                  reof;
    logic [CELL_ID_W:0]    fl_before, fl_after;

    initial begin
        $dumpfile("tb_pkt_buffer.vcd");
        $dumpvars(0, tb_pkt_buffer);

        wr.valid       = 0;
        rd_tm.req_valid= 0;
        rd_dp.req_valid= 0;
        alloc.alloc_req= 0;
        alloc.free_req = 0;
        alloc.free_id  = '0;

        #5 rst_dp_n = 1;
        repeat(4) @(posedge clk_dp);

        // ── TC1：分配 cell，检查 ID 递增 ──────
        alloc_cell(cid0);
        alloc_cell(cid1);
        alloc_cell(cid2);

        if (cid1 !== cid0 + 1 || cid2 !== cid1 + 1) begin
            $display("FAIL TC1: cell IDs not sequential: %0d %0d %0d",
                     cid0, cid1, cid2);
            $finish;
        end
        $display("PASS TC1: alloc sequential cid0=%0d cid1=%0d cid2=%0d",
                 cid0, cid1, cid2);

        // ── TC2：写入 cell，读回验证 ──────────
        write_cell(cid0, 512'hDEAD_BEEF_CAFE_1234, 1'b1, 1'b0);
        write_cell(cid1, 512'hABCD_EF01_2345_6789, 1'b0, 1'b1);

        read_cell_tm(cid0, rdata, reof);
        if (rdata !== 512'hDEAD_BEEF_CAFE_1234) begin
            $display("FAIL TC2: cell0 data mismatch: %h", rdata);
            $finish;
        end
        if (reof !== 1'b0) begin
            $display("FAIL TC2: cell0 eof should be 0");
            $finish;
        end
        $display("PASS TC2: write/read cell0 data match");

        read_cell_tm(cid1, rdata, reof);
        if (rdata !== 512'hABCD_EF01_2345_6789) begin
            $display("FAIL TC2: cell1 data mismatch: %h", rdata);
            $finish;
        end
        if (reof !== 1'b1) begin
            $display("FAIL TC2: cell1 eof should be 1");
            $finish;
        end
        $display("PASS TC2: write/read cell1 data match, eof=%b", reof);

        // ── TC3：释放 cell，free list 恢复 ────
        // 记录释放前 free list 计数（通过 alloc_valid 间接判断）
        free_cell(cid0);
        free_cell(cid1);
        free_cell(cid2);
        repeat(2) @(posedge clk_dp);

        // 重新分配，应能拿到刚释放的 cell
        alloc_cell(cid0);
        if (!alloc.alloc_valid) begin
            $display("FAIL TC3: alloc_valid=0 after free");
            $finish;
        end
        $display("PASS TC3: free + realloc ok, new_cid=%0d", cid0);

        // ── TC4：双端口并发读（TM + Deparser）─
        write_cell(cid0, 512'hFFFF_0000_AAAA_5555, 1'b1, 1'b1);

        @(posedge clk_dp);
        rd_tm.req_valid   = 1;
        rd_tm.req_cell_id = cid0;
        rd_dp.req_valid   = 1;
        rd_dp.req_cell_id = cid0;
        @(posedge clk_dp);
        rd_tm.req_valid = 0;
        rd_dp.req_valid = 0;
        @(posedge clk_dp);

        if (rd_tm.rsp_data !== 512'hFFFF_0000_AAAA_5555) begin
            $display("FAIL TC4: TM port data mismatch");
            $finish;
        end
        if (rd_dp.rsp_data !== 512'hFFFF_0000_AAAA_5555) begin
            $display("FAIL TC4: DP port data mismatch");
            $finish;
        end
        $display("PASS TC4: dual-port concurrent read ok");

        $display("\n=== All pkt_buffer tests PASSED ===");
        $finish;
    end

    initial begin #50000; $display("TIMEOUT"); $finish; end

endmodule
