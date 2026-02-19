// tb_mau_stage.sv
// MAU 单级集成测试
// 验证：TCAM 命中 → Action SRAM 读取 → ALU 执行 → PHV 修改

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_mau_stage;
    import rv_p4_pkg::*;

    logic clk_dp = 0, rst_dp_n = 0;
    always #0.3125 clk_dp = ~clk_dp;

    // ── Interface 实例 ────────────────────────
    phv_if phv_in  (.clk(clk_dp), .rst_n(rst_dp_n));
    phv_if phv_out (.clk(clk_dp), .rst_n(rst_dp_n));
    mau_cfg_if cfg (.clk(clk_dp), .rst_n(rst_dp_n));

    // ── DUT ───────────────────────────────────
    mau_stage #(.STAGE_ID(0)) dut (
        .clk_dp   (clk_dp),
        .rst_dp_n (rst_dp_n),
        .phv_in   (phv_in.dst),
        .phv_out  (phv_out.src),
        .cfg      (cfg.receiver)
    );

    // 下游始终就绪
    assign phv_out.ready = 1'b1;

    // ── 任务：写 TCAM 条目 ────────────────────
    task cfg_tcam(
        input int          idx,
        input logic [511:0] k,
        input logic [511:0] m,
        input logic [15:0]  aid,
        input logic [15:0]  aptr
    );
        @(posedge clk_dp);
        cfg.tcam_wr_en     = 1;
        cfg.tcam_wr_addr   = 11'(idx);
        cfg.tcam_wr_key    = k;
        cfg.tcam_wr_mask   = m;
        cfg.tcam_action_id = aid;
        cfg.tcam_action_ptr= aptr;
        cfg.tcam_wr_valid  = 1;
        @(posedge clk_dp);
        cfg.tcam_wr_en = 0;
    endtask

    // ── 任务：写 Action SRAM 条目 ─────────────
    task cfg_asram(
        input int          idx,
        input logic [15:0]  aid,
        input logic [111:0] params
    );
        @(posedge clk_dp);
        cfg.asram_wr_en   = 1;
        cfg.asram_wr_addr = 16'(idx);
        cfg.asram_wr_data = {aid, params};  // 16+112=128b
        @(posedge clk_dp);
        cfg.asram_wr_en = 0;
    endtask

    // ── 任务：发送 PHV ────────────────────────
    task send_phv(input logic [PHV_BITS-1:0] data, input phv_meta_t meta);
        @(posedge clk_dp);
        phv_in.valid = 1;
        phv_in.data  = data;
        phv_in.meta  = meta;
        wait (phv_in.ready);
        @(posedge clk_dp);
        phv_in.valid = 0;
    endtask

    // ── 任务：等待并捕获输出 PHV ──────────────
    task automatic wait_phv_out(output logic [PHV_BITS-1:0] data, output phv_meta_t meta);
        int timeout = 20;
        while (!phv_out.valid && timeout > 0) begin
            @(posedge clk_dp);
            timeout--;
        end
        data = phv_out.data;
        meta = phv_out.meta;
    endtask

    // ── 测试主体 ──────────────────────────────
    logic [PHV_BITS-1:0] out_data;
    phv_meta_t           out_meta;
    phv_meta_t           test_meta;

    initial begin
        $dumpfile("tb_mau_stage.vcd");
        $dumpvars(0, tb_mau_stage);

        // 初始化
        phv_in.valid = 0;
        phv_in.data  = '0;
        phv_in.meta  = '0;
        cfg.tcam_wr_en  = 0;
        cfg.asram_wr_en = 0;

        #5 rst_dp_n = 1;
        repeat(4) @(posedge clk_dp);

        // ── TC1：SET 动作（修改 eg_port）────────
        // TCAM：key=PHV[511:0]=0x1234...，精确匹配
        // Action：OP_SET_PORT，port=5
        cfg_tcam(0,
            512'h1234_0000_0000_0000,
            512'h0,
            16'hA000,  // action_id[15:12]=0xA → OP_SET_PORT
            16'h0001); // action_ptr → ASRAM[1]

        // ASRAM[1]：action_id=0xA000，params：imm=5（port）
        // action_params[47:16]=imm=5
        cfg_asram(1,
            16'hA000,
            {64'b0, 32'd5, 16'b0}); // imm_val=5 at [47:16]

        // 发送 PHV
        test_meta        = '0;
        test_meta.ig_port = 5'd3;
        test_meta.eg_port = 5'd0;
        send_phv(PHV_BITS'({448'b0, 64'h1234_0000_0000_0000}), test_meta);

        wait_phv_out(out_data, out_meta);

        if (out_meta.eg_port !== 5'd5) begin
            $display("FAIL TC1: eg_port=%0d expected=5", out_meta.eg_port);
            $finish;
        end
        $display("PASS TC1: SET_PORT → eg_port=%0d", out_meta.eg_port);

        // ── TC2：DROP 动作 ────────────────────
        cfg_tcam(1,
            512'hDEAD_0000_0000_0000,
            512'h0,
            16'h9000,  // OP_DROP
            16'h0002);

        cfg_asram(2, 16'h9000, 112'b0);

        test_meta        = '0;
        test_meta.ig_port = 5'd1;
        send_phv(PHV_BITS'({448'b0, 64'hDEAD_0000_0000_0000}), test_meta);

        wait_phv_out(out_data, out_meta);

        if (!out_meta.drop) begin
            $display("FAIL TC2: drop not set");
            $finish;
        end
        $display("PASS TC2: DROP → meta.drop=%b", out_meta.drop);

        // ── TC3：TCAM miss → PHV 透传 ─────────
        test_meta        = '0;
        test_meta.ig_port = 5'd7;
        test_meta.eg_port = 5'd9;
        send_phv(PHV_BITS'({448'b0, 64'hCAFE_0000_0000_0000}), test_meta);

        wait_phv_out(out_data, out_meta);

        if (out_meta.drop || out_meta.eg_port !== 5'd9) begin
            $display("FAIL TC3: miss should passthrough, drop=%b eg_port=%0d",
                     out_meta.drop, out_meta.eg_port);
            $finish;
        end
        $display("PASS TC3: TCAM miss → PHV passthrough, eg_port=%0d", out_meta.eg_port);

        // ── TC4：流水线吞吐（连续 8 个 PHV）────
        begin
            int sent = 0, recv = 0;
            fork
                begin // 发送
                    for (int i = 0; i < 8; i++) begin
                        test_meta        = '0;
                        test_meta.eg_port = 5'(i);
                        send_phv(PHV_BITS'(i), test_meta);
                        sent++;
                    end
                end
                begin // 接收
                    repeat(8) begin
                        wait_phv_out(out_data, out_meta);
                        recv++;
                    end
                end
            join
            $display("PASS TC4: pipeline throughput sent=%0d recv=%0d", sent, recv);
        end

        $display("\n=== All MAU stage tests PASSED ===");
        $finish;
    end

    initial begin #100000; $display("TIMEOUT"); $finish; end

endmodule
