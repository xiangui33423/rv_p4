// tb_top.sv
// Integration testbench for rv_p4_top
// TC1: basic packet forwarding — send Ethernet frame on port 0, verify TX output

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_top;
    import rv_p4_pkg::*;

    // ─────────────────────────────────────────
    // Clock generation
    // clk_dp   = 1.6  GHz → period = 0.625 ns
    // clk_ctrl = 200  MHz → period = 5.0   ns
    // clk_mac  = 390.625 MHz → period = 2.56 ns
    // clk_cpu  = 1.5  GHz → period = 0.667 ns
    // ─────────────────────────────────────────
    localparam real T_DP   = 0.625;
    localparam real T_CTRL = 5.0;
    localparam real T_MAC  = 2.56;
    localparam real T_CPU  = 0.667;

    logic clk_dp   = 0;
    logic clk_ctrl = 0;
    logic clk_mac  = 0;
    logic clk_cpu  = 0;
    logic rst_n    = 0;

    always #(T_DP   / 2.0) clk_dp   = ~clk_dp;
    always #(T_CTRL / 2.0) clk_ctrl = ~clk_ctrl;
    always #(T_MAC  / 2.0) clk_mac  = ~clk_mac;
    always #(T_CPU  / 2.0) clk_cpu  = ~clk_cpu;

    // ─────────────────────────────────────────
    // DUT ports
    // ─────────────────────────────────────────
    logic [31:0]        rx_valid   = '0;
    logic [31:0]        rx_sof     = '0;
    logic [31:0]        rx_eof     = '0;
    logic [31:0][6:0]   rx_eop_len = '0;
    logic [31:0][511:0] rx_data    = '0;
    logic [31:0]        rx_ready;

    logic [31:0]        tx_valid;
    logic [31:0]        tx_sof;
    logic [31:0]        tx_eof;
    logic [31:0][6:0]   tx_eop_len;
    logic [31:0][511:0] tx_data;
    logic [31:0]        tx_ready   = '1;  // always accept TX

    logic        pcie_clk          = 0;
    logic [255:0] pcie_rx_data     = '0;
    logic [255:0] pcie_tx_data;
    logic        pcie_rx_valid     = 0;
    logic        pcie_tx_valid;

    logic tck = 0, tms = 0, tdi = 0, tdo;

    // Test backdoor: parser TCAM write
    logic                         tb_parser_wr_en   = 0;
    logic [7:0]                   tb_parser_wr_addr = '0;
    logic [PARSER_TCAM_WIDTH-1:0] tb_parser_wr_data = '0;

    // ─────────────────────────────────────────
    // DUT instantiation
    // ─────────────────────────────────────────
    rv_p4_top u_dut (
        .clk_dp            (clk_dp),
        .clk_ctrl          (clk_ctrl),
        .clk_cpu           (clk_cpu),
        .clk_mac           (clk_mac),
        .rst_n             (rst_n),
        .rx_valid          (rx_valid),
        .rx_sof            (rx_sof),
        .rx_eof            (rx_eof),
        .rx_eop_len        (rx_eop_len),
        .rx_data           (rx_data),
        .rx_ready          (rx_ready),
        .tx_valid          (tx_valid),
        .tx_sof            (tx_sof),
        .tx_eof            (tx_eof),
        .tx_eop_len        (tx_eop_len),
        .tx_data           (tx_data),
        .tx_ready          (tx_ready),
        .pcie_clk          (pcie_clk),
        .pcie_rx_data      (pcie_rx_data),
        .pcie_tx_data      (pcie_tx_data),
        .pcie_rx_valid     (pcie_rx_valid),
        .pcie_tx_valid     (pcie_tx_valid),
        .tck(tck), .tms(tms), .tdi(tdi), .tdo(tdo),
        .tb_parser_wr_en   (tb_parser_wr_en),
        .tb_parser_wr_addr (tb_parser_wr_addr),
        .tb_parser_wr_data (tb_parser_wr_data)
    );

    // ─────────────────────────────────────────
    // Test state
    // ─────────────────────────────────────────
    int tc1_result = 0;  // 0=pending, 1=pass, 2=fail

    // ─────────────────────────────────────────
    // Helper tasks
    // ─────────────────────────────────────────
    task wait_dp(input int n);
        repeat (n) @(posedge clk_dp);
    endtask

    task wait_mac(input int n);
        repeat (n) @(posedge clk_mac);
    endtask

    // ─────────────────────────────────────────
    // Parser TCAM entry builder
    // Entry format (640b):
    //  [639:634] key_state      (6b)
    //  [633:570] key_window     (64b)
    //  [569:564] mask_state     (6b)  — 1=don't care
    //  [563:506] padding        (58b)
    //  [505:442] mask_window    (64b) — 1=don't care
    //  [441:436] next_state     (6b)
    //  [435:428] extract_offset (8b)
    //  [427:420] extract_len    (8b)
    //  [419:410] phv_dst_offset (10b)
    //  [409:402] hdr_advance    (8b)
    //  [401]     valid
    //  [400:0]   reserved
    // ─────────────────────────────────────────
    function automatic logic [PARSER_TCAM_WIDTH-1:0] make_tcam_entry(
        input logic [5:0]  key_state,
        input logic [63:0] key_window,
        input logic [5:0]  mask_state,
        input logic [63:0] mask_window,
        input logic [5:0]  next_st,
        input logic [7:0]  ext_off,
        input logic [7:0]  ext_len,
        input logic [9:0]  phv_dst,
        input logic [7:0]  hdr_adv,
        input logic        valid
    );
        logic [PARSER_TCAM_WIDTH-1:0] e;
        e = '0;
        e[639:634] = key_state;
        e[633:570] = key_window;
        e[569:564] = mask_state;
        e[505:442] = mask_window;
        e[441:436] = next_st;
        e[435:428] = ext_off;
        e[427:420] = ext_len;
        e[419:410] = phv_dst;
        e[409:402] = hdr_adv;
        e[401]     = valid;
        return e;
    endfunction

    // ─────────────────────────────────────────
    // Main test sequence
    // ─────────────────────────────────────────
    initial begin
        $display("TB: Starting rv_p4_top integration test");

        // ── Reset ────────────────────────────
        rst_n = 0;
        repeat (20) @(posedge clk_dp);
        rst_n = 1;
        $display("TB: Reset released at t=%0t", $time);

        // Wait for reset synchronizers to propagate
        wait_dp(10);

        // ── Pre-load parser TCAM via backdoor ─
        // Entry 0: match state=1 (ST_ETHERNET), any window → ACCEPT (0x3F)
        // This causes the parser to accept any Ethernet frame and emit PHV.
        // eg_port defaults to 0, so packet goes to port 0 TX queue.
        @(posedge clk_dp);
        tb_parser_wr_en   = 1'b1;
        tb_parser_wr_addr = 8'd0;
        tb_parser_wr_data = make_tcam_entry(
            .key_state   (6'd1),
            .key_window  (64'h0),
            .mask_state  (6'b000000),                // state must match exactly
            .mask_window (64'hFFFF_FFFF_FFFF_FFFF),  // window = full don't care
            .next_st     (6'h3F),                    // ACCEPT
            .ext_off     (8'd0),
            .ext_len     (8'd6),
            .phv_dst     (10'd0),
            .hdr_adv     (8'd14),
            .valid       (1'b1)
        );
        @(posedge clk_dp);
        tb_parser_wr_en   = 1'b0;
        tb_parser_wr_addr = '0;
        tb_parser_wr_data = '0;

        $display("TB: Parser TCAM entry 0 loaded at t=%0t", $time);
        wait_dp(5);

        // ── TC1: Send Ethernet frame on port 0 ──
        $display("TB: TC1 — sending Ethernet frame on port 0");

        // Build a minimal Ethernet frame in one 64B cell
        // Byte layout in 512b vector (MSB = byte 0):
        //   [511:464] DST MAC  ff:ff:ff:ff:ff:ff
        //   [463:416] SRC MAC  00:11:22:33:44:55
        //   [415:400] EtherType 0x0800
        //   [399:...]  payload 0xAB × 46 bytes
        begin
            logic [511:0] frame;
            frame = '0;
            frame[511:464] = 48'hFFFF_FFFF_FFFF;
            frame[463:416] = 48'h0011_2233_4455;
            frame[415:400] = 16'h0800;
            for (int b = 14; b < 60; b++)
                frame[(63-b)*8 +: 8] = 8'hAB;

            @(posedge clk_mac);
            rx_valid[0]   = 1'b1;
            rx_sof[0]     = 1'b1;
            rx_eof[0]     = 1'b1;
            rx_eop_len[0] = 7'd60;
            rx_data[0]    = frame;

            // Hold until arbiter accepts
            @(posedge clk_mac);
            while (!rx_ready[0]) @(posedge clk_mac);

            @(posedge clk_mac);
            rx_valid[0]   = 1'b0;
            rx_sof[0]     = 1'b0;
            rx_eof[0]     = 1'b0;
            rx_eop_len[0] = '0;
            rx_data[0]    = '0;
        end

        $display("TB: Frame injected at t=%0t, waiting for TX...", $time);

        // ── Poll for TX output (up to 2000 clk_dp cycles) ──
        tc1_result = 0;
        for (int cyc = 0; cyc < 2000; cyc++) begin
            @(posedge clk_dp);
            for (int p = 0; p < NUM_PORTS; p++) begin
                if (tx_valid[p] && tc1_result == 0) begin
                    $display("TB: TX on port %0d at t=%0t sof=%b eof=%b",
                             p, $time, tx_sof[p], tx_eof[p]);
                    tc1_result = 1;
                end
            end
            if (tc1_result != 0) break;
        end
        if (tc1_result == 0) tc1_result = 2;

        // ── Report ────────────────────────────
        if (tc1_result == 1)
            $display("PASS: TC1 basic packet forwarding");
        else
            $display("FAIL: TC1 timeout — no TX output after 2000 dp cycles");

        wait_dp(10);
        $finish;
    end

    // ─────────────────────────────────────────
    // Watchdog: hard limit 100 us
    // ─────────────────────────────────────────
    initial begin
        #100000;
        $display("FAIL: Watchdog timeout at t=%0t", $time);
        $finish;
    end

endmodule
