// tb_parser.sv
// P4 Parser 集成测试
// 验证：以太网帧解析 → PHV 构建 → cell 写入包缓冲

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_parser;
    import rv_p4_pkg::*;

    logic clk_dp = 0, clk_mac = 0, rst_dp_n = 0;
    always #0.3125 clk_dp  = ~clk_dp;
    always #1.28   clk_mac = ~clk_mac;

    // ── Interface 实例 ────────────────────────
    mac_rx_if     mac_rx    (.clk(clk_mac), .rst_n(rst_dp_n));
    pb_wr_if      pb_wr     (.clk(clk_dp),  .rst_n(rst_dp_n));
    cell_alloc_if cell_alloc(.clk(clk_dp),  .rst_n(rst_dp_n));
    phv_if        phv_out   (.clk(clk_dp),  .rst_n(rst_dp_n));
    apb_if        csr       (.clk(clk_dp),  .rst_n(rst_dp_n));

    // Parser TCAM 直写
    logic                         fsm_wr_en   = 0;
    logic [7:0]                   fsm_wr_addr = '0;
    logic [PARSER_TCAM_WIDTH-1:0] fsm_wr_data = '0;

    // ── DUT ───────────────────────────────────
    p4_parser dut (
        .clk_dp      (clk_dp),
        .rst_dp_n    (rst_dp_n),
        .rx          (mac_rx.dst),
        .pb_wr       (pb_wr.src),
        .cell_alloc  (cell_alloc.requester),
        .phv_out     (phv_out.src),
        .fsm_wr_en   (fsm_wr_en),
        .fsm_wr_addr (fsm_wr_addr),
        .fsm_wr_data (fsm_wr_data),
        .csr         (csr.slave)
    );

    assign pb_wr.ready        = 1'b1;
    assign phv_out.ready      = 1'b1;
    assign csr.psel           = 0;
    assign csr.penable        = 0;
    assign csr.pwrite         = 0;
    assign csr.paddr          = '0;
    assign csr.pwdata         = '0;

    // ── 模拟 Cell 分配器 ──────────────────────
    logic [CELL_ID_W-1:0] alloc_counter = '0;
    assign cell_alloc.alloc_valid = 1'b1;
    assign cell_alloc.alloc_empty = 1'b0;
    assign cell_alloc.alloc_id    = alloc_counter;
    always_ff @(posedge clk_dp)
        if (cell_alloc.alloc_req && cell_alloc.alloc_valid)
            alloc_counter <= alloc_counter + 1'b1;

    // ── 任务：写 Parser TCAM 条目 ─────────────
    // PARSER_TCAM_WIDTH=640b 位域：
    //  [639:634] key_state(6b)  [633:570] key_window(64b)
    //  [569:564] mask_state(6b) [563:500] mask_window(64b) [499:442] padding
    //  [441:436] next_state(6b) [435:428] extract_offset(8b)
    //  [427:420] extract_len(8b) [419:410] phv_dst_offset(10b)
    //  [409:402] hdr_advance(8b) [401] valid [400:0] reserved
    task automatic write_fsm(
        input logic [5:0]  cur_state,
        input logic [63:0] key_win,
        input logic [63:0] mask_win,
        input logic [5:0]  nxt_state,
        input logic [7:0]  ext_off,
        input logic [7:0]  ext_len,
        input logic [9:0]  phv_dst,
        input logic [7:0]  hdr_adv,
        input int          idx
    );
        logic [PARSER_TCAM_WIDTH-1:0] entry = '0;
        entry[639:634] = cur_state;
        entry[633:570] = key_win;
        entry[569:564] = 6'b0;       // mask_state = 0 (exact)
        entry[563:500] = mask_win;
        entry[441:436] = nxt_state;
        entry[435:428] = ext_off;
        entry[427:420] = ext_len;
        entry[419:410] = phv_dst;
        entry[409:402] = hdr_adv;
        entry[401]     = 1'b1;       // valid

        @(posedge clk_dp);
        fsm_wr_en   = 1;
        fsm_wr_addr = 8'(idx);
        fsm_wr_data = entry;
        @(posedge clk_dp);
        fsm_wr_en = 0;
    endtask

    // ── 任务：发送一个 cell（等 ready 后发送）──
    task automatic send_cell(
        input logic [4:0]   port,
        input logic         sof,
        input logic         eof,
        input logic [6:0]   eop_len,
        input logic [511:0] data
    );
        // 等待 parser 就绪（rx.ready 高）
        while (!mac_rx.ready) @(posedge clk_mac);
        @(posedge clk_mac);
        mac_rx.valid   = 1;
        mac_rx.port    = port;
        mac_rx.sof     = sof;
        mac_rx.eof     = eof;
        mac_rx.eop_len = eop_len;
        mac_rx.data    = data;
        @(posedge clk_mac);
        mac_rx.valid = 0;
        mac_rx.sof   = 0;
        mac_rx.eof   = 0;
    endtask

    // ── 构建以太网 + IPv4 帧（64B）────────────
    function automatic logic [511:0] build_eth_ipv4(
        input logic [47:0] dst_mac,
        input logic [47:0] src_mac,
        input logic [31:0] src_ip,
        input logic [31:0] dst_ip,
        input logic [7:0]  proto
    );
        logic [511:0] frame = '0;
        frame[511:464] = dst_mac;
        frame[463:416] = src_mac;
        frame[415:400] = 16'h0800;  // EtherType IPv4
        frame[399:392] = 8'h45;     // ver=4, ihl=5
        frame[391:384] = 8'h00;
        frame[383:368] = 16'd50;
        frame[367:352] = 16'h0001;
        frame[351:336] = 16'h0000;
        frame[335:328] = 8'd64;     // TTL
        frame[327:320] = proto;
        frame[319:304] = 16'h0000;
        frame[303:272] = src_ip;
        frame[271:240] = dst_ip;
        return frame;
    endfunction

    // ── 捕获 PHV（持久 flag）─────────────────
    logic [PHV_BITS-1:0] captured_phv;
    phv_meta_t           captured_meta;
    logic                phv_flag = 0;  // 有新 PHV 待读取

    always_ff @(posedge clk_dp)
        if (phv_out.valid && phv_out.ready) begin
            captured_phv  <= phv_out.data;
            captured_meta <= phv_out.meta;
            phv_flag      <= 1'b1;
        end

    // ── 等待 PHV（检查 flag，带超时）─────────
    task automatic wait_phv(input int max_cycles, output logic ok);
        int cnt = 0;
        ok = 0;
        while (cnt < max_cycles) begin
            @(posedge clk_dp);
            cnt++;
            if (phv_flag) begin
                ok       = 1;
                phv_flag = 0;  // 清除 flag
                break;
            end
        end
    endtask

    // ── 测试主体 ──────────────────────────────
    logic ok;

    initial begin
        $dumpfile("tb_parser.vcd");
        $dumpvars(0, tb_parser);

        mac_rx.valid   = 0;
        mac_rx.port    = '0;
        mac_rx.sof     = 0;
        mac_rx.eof     = 0;
        mac_rx.eop_len = '0;
        mac_rx.data    = '0;

        #10 rst_dp_n = 1;
        repeat(4) @(posedge clk_dp);

        // ── 预装 Parser TCAM ──────────────────
        // 状态 1（ST_ETHERNET）：
        //   key_window = 前 8B（dst_mac 高 8B），全通配
        //   → 提取 dst_mac(6B) 到 PHV[0]，推进 14B，进入状态 2
        write_fsm(
            6'd1,           // cur_state = ST_ETHERNET
            64'h0,          // key_window（不关心）
            64'hFFFFFFFFFFFFFFFF, // mask = 全通配
            6'h3F,          // next_state = ACCEPT(0x3F)
            8'd0,           // extract_offset = 0
            8'd6,           // extract_len = 6B (dst_mac)
            10'd0,          // phv_dst = PHV[0]
            8'd14,          // hdr_advance = 14B (eth header)
            0               // TCAM index 0
        );

        repeat(4) @(posedge clk_dp);

        // ── TC1：单 cell IPv4/TCP 帧（端口 0）──
        send_cell(5'd0, 1'b1, 1'b1, 7'd64,
            build_eth_ipv4(
                48'hAABBCCDDEEFF, 48'h112233445566,
                32'h0A000001, 32'h0A000002, 8'd6));

        wait_phv(500, ok);
        if (!ok) begin $display("FAIL TC1: PHV not emitted"); $finish; end
        if (captured_meta.ig_port !== 5'd0) begin
            $display("FAIL TC1: ig_port=%0d expected=0", captured_meta.ig_port);
            $finish;
        end
        if (captured_meta.drop) begin
            $display("FAIL TC1: unexpected drop"); $finish;
        end
        $display("PASS TC1: IPv4/TCP parsed ig_port=%0d pkt_len=%0d",
                 captured_meta.ig_port, captured_meta.pkt_len);

        // ── TC2：多 cell 帧（2 cells，端口 1）──
        send_cell(5'd1, 1'b1, 1'b0, 7'd64,
            build_eth_ipv4(
                48'hFFFFFFFFFFFF, 48'h001122334455,
                32'hC0A80001, 32'hC0A80002, 8'd17));
        send_cell(5'd1, 1'b0, 1'b1, 7'd40, 512'hDEAD_BEEF);

        wait_phv(600, ok);
        if (!ok) begin $display("FAIL TC2: PHV not emitted"); $finish; end
        if (captured_meta.ig_port !== 5'd1) begin
            $display("FAIL TC2: ig_port=%0d expected=1", captured_meta.ig_port);
            $finish;
        end
        $display("PASS TC2: multi-cell frame ig_port=%0d pkt_len=%0d",
                 captured_meta.ig_port, captured_meta.pkt_len);

        // ── TC3：连续帧（端口 2）──────────────
        send_cell(5'd2, 1'b1, 1'b1, 7'd64,
            build_eth_ipv4(48'h0, 48'h0, 32'h01020304, 32'h05060708, 8'd1));

        wait_phv(500, ok);
        if (!ok) begin $display("FAIL TC3: PHV not emitted"); $finish; end
        $display("PASS TC3: back-to-back frame ig_port=%0d", captured_meta.ig_port);

        $display("\n=== All Parser tests PASSED ===");
        $finish;
    end

    initial begin #1000000; $display("TIMEOUT"); $finish; end

endmodule
