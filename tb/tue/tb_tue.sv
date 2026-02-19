// tb_tue.sv
// TUE 单元测试
// 验证：APB 写寄存器 → commit → MAU TCAM/SRAM 更新

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_tue;
    import rv_p4_pkg::*;

    logic clk_ctrl = 0, clk_dp = 0, rst_ctrl_n = 0;
    always #2.5  clk_ctrl = ~clk_ctrl;  // 200 MHz
    always #0.3125 clk_dp = ~clk_dp;   // 1.6 GHz

    // ── Interface 实例 ────────────────────────
    apb_if      csr     (.clk(clk_ctrl), .rst_n(rst_ctrl_n));
    tue_req_if  req     (.clk(clk_ctrl), .rst_n(rst_ctrl_n));
    mau_cfg_if  mau_cfg [NUM_MAU_STAGES] (.clk(clk_dp), .rst_n(rst_ctrl_n));

    logic                          parser_wr_en;
    logic [7:0]                    parser_wr_addr;
    logic [PARSER_TCAM_WIDTH-1:0]  parser_wr_data;

    // ── DUT ───────────────────────────────────
    tue dut (
        .clk_ctrl    (clk_ctrl),
        .rst_ctrl_n  (rst_ctrl_n),
        .clk_dp      (clk_dp),
        .csr         (csr.slave),
        .req         (req.slave),
        .mau_cfg     (mau_cfg),
        .parser_wr_en   (parser_wr_en),
        .parser_wr_addr (parser_wr_addr),
        .parser_wr_data (parser_wr_data)
    );

    // ── APB 写任务 ────────────────────────────
    task automatic apb_write(input logic [11:0] addr, input logic [31:0] data);
        @(posedge clk_ctrl);
        csr.psel    = 1; csr.penable = 0;
        csr.pwrite  = 1; csr.paddr   = addr; csr.pwdata = data;
        @(posedge clk_ctrl);
        csr.penable = 1;
        @(posedge clk_ctrl);
        csr.psel = 0; csr.penable = 0;
    endtask

    // ── APB 读任务 ────────────────────────────
    task automatic apb_read(input logic [11:0] addr, output logic [31:0] data);
        @(posedge clk_ctrl);
        csr.psel    = 1; csr.penable = 0;
        csr.pwrite  = 0; csr.paddr   = addr;
        @(posedge clk_ctrl);
        csr.penable = 1;
        @(posedge clk_ctrl);
        data = csr.prdata;
        csr.psel = 0; csr.penable = 0;
    endtask

    // ── 等待 TUE 完成（先等 busy，再等 done）──
    task automatic wait_done;
        logic [31:0] st;
        int timeout = 5000;
        // 等 busy
        while (timeout-- > 0) begin
            apb_read(TUE_REG_STATUS, st);
            if (st[1:0] == 2'b01) break;
        end
        // 等 done/idle
        while (timeout-- > 0) begin
            apb_read(TUE_REG_STATUS, st);
            if (st[1:0] != 2'b01) break;
        end
    endtask

    // ── 等待 stage 0 TCAM 写入 ────────────────
    task automatic wait_tcam_wr_s0(
        input  int          max_cycles,
        output logic        ok,
        output logic [10:0] addr_out,
        output logic [15:0] aid_out
    );
        int cnt = 0;
        ok = 0;
        while (cnt < max_cycles) begin
            @(posedge clk_dp); cnt++;
            if (mau_cfg[0].tcam_wr_en) begin
                ok = 1; addr_out = mau_cfg[0].tcam_wr_addr;
                aid_out = mau_cfg[0].tcam_action_id; break;
            end
        end
    endtask

    // ── 等待 stage 0 ASRAM 写入 ───────────────
    task automatic wait_asram_wr_s0(input int max_cycles, output logic ok);
        int cnt = 0; ok = 0;
        while (cnt < max_cycles) begin
            @(posedge clk_dp); cnt++;
            if (mau_cfg[0].asram_wr_en) begin ok = 1; break; end
        end
    endtask

    // ── 写 key/mask（16 × 32b）───────────────
    task automatic write_key(input logic [11:0] base, input logic [511:0] val);
        for (int i = 0; i < 16; i++)
            apb_write(base + 12'(i*4), val[i*32 +: 32]);
    endtask

    // ── 测试主体 ──────────────────────────────
    logic        ok;
    logic [10:0] tcam_addr;
    logic [15:0] tcam_aid;
    logic [31:0] rdata;

    initial begin
        $dumpfile("tb_tue.vcd");
        $dumpvars(0, tb_tue);

        csr.psel = 0; csr.penable = 0; csr.pwrite = 0;
        csr.paddr = '0; csr.pwdata = '0;
        req.valid = 0; req.req = '0;

        #10 rst_ctrl_n = 1;
        repeat(4) @(posedge clk_ctrl);

        // ── TC1：INSERT → stage 0 TCAM/ASRAM 写入 ──
        apb_write(TUE_REG_CMD,       32'd0);   // INSERT
        apb_write(TUE_REG_STAGE,     32'd0);   // stage 0
        apb_write(TUE_REG_TABLE_ID,  32'd5);
        write_key(TUE_REG_KEY_0,     512'hDEADBEEF);
        write_key(TUE_REG_MASK_0,    512'h0);
        apb_write(TUE_REG_ACTION_ID, 32'h1001);
        apb_write(TUE_REG_ACTION_P0, 32'h5);
        apb_write(TUE_REG_ACTION_P1, 32'h0);
        apb_write(TUE_REG_ACTION_P2, 32'h0);
        apb_write(TUE_REG_COMMIT,    32'h1);

        // 在 clk_dp 上等 TCAM 写入（最多 2000 cycles）
        fork
            wait_tcam_wr_s0( 2000, ok, tcam_addr, tcam_aid);
        join

        if (!ok) begin $display("FAIL TC1: no TCAM write"); $finish; end
        $display("PASS TC1: INSERT tcam_addr=%0d action_id=%h", tcam_addr, tcam_aid);

        // 等 ASRAM 写入
        fork
            wait_asram_wr_s0( 100, ok);
        join
        if (!ok) begin $display("FAIL TC1: no ASRAM write"); $finish; end
        $display("PASS TC1: ASRAM write seen");

        // ── TC2：DELETE → valid=0 ─────────────
        apb_write(TUE_REG_CMD,      32'd1);   // DELETE
        apb_write(TUE_REG_STAGE,    32'd0);
        apb_write(TUE_REG_TABLE_ID, 32'd5);
        apb_write(TUE_REG_COMMIT,   32'h1);

        fork
            wait_tcam_wr_s0( 2000, ok, tcam_addr, tcam_aid);
        join
        if (!ok) begin $display("FAIL TC2: no TCAM write for DELETE"); $finish; end
        if (mau_cfg[0].tcam_wr_valid) begin
            $display("FAIL TC2: DELETE should set valid=0");
            $finish;
        end
        $display("PASS TC2: DELETE tcam_wr_valid=%b", mau_cfg[0].tcam_wr_valid);

        // ── TC3：STATUS 寄存器 ────────────────
        wait_done;
        apb_read(TUE_REG_STATUS, rdata);
        if (rdata[1:0] !== 2'b00) begin
            $display("FAIL TC3: status not idle: %b", rdata[1:0]);
            $finish;
        end
        $display("PASS TC3: STATUS=idle");

        // ── TC4：MODIFY stage 3，不影响 stage 0 ─
        apb_write(TUE_REG_CMD,       32'd2);   // MODIFY
        apb_write(TUE_REG_STAGE,     32'd3);
        apb_write(TUE_REG_TABLE_ID,  32'd10);
        apb_write(TUE_REG_ACTION_ID, 32'h2002);
        apb_write(TUE_REG_COMMIT,    32'h1);

        // stage 0 不应有写入（最多等 2000 cycles）
        fork
            wait_tcam_wr_s0( 2000, ok, tcam_addr, tcam_aid);
        join
        if (ok) begin
            $display("FAIL TC4: stage 3 op wrote to stage 0");
            $finish;
        end
        $display("PASS TC4: MODIFY stage 3 did not affect stage 0");

        $display("\n=== All TUE tests PASSED ===");
        $finish;
    end

    initial begin #500000; $display("TIMEOUT"); $finish; end

endmodule
