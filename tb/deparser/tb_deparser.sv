// tb_deparser.sv
// Testbench for deparser.sv
// TC1: PHV eg_port=2, verify TX on port 2
// TC2: TTL decrement -> checksum update
// TC3: drop flag -> no TX output

`timescale 1ns/1ps
`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tb_deparser;
    import rv_p4_pkg::*;

    // ── Clock / reset ────────────────────────────────────────────────
    logic clk, rst_n;
    initial clk = 0;
    always #5 clk = ~clk;   // 100 MHz

    initial begin
        rst_n = 0;
        repeat(4) @(posedge clk);
        rst_n = 1;
    end

    // ── Interface instances ──────────────────────────────────────────
    phv_if      phv_bus (.clk(clk), .rst_n(rst_n));
    pb_rd_if    pb_bus  (.clk(clk), .rst_n(rst_n));
    apb_if      apb_bus (.clk(clk), .rst_n(rst_n));

    // ── DUT ─────────────────────────────────────────────────────────
    logic [31:0]        tx_valid;
    logic [31:0]        tx_sof;
    logic [31:0]        tx_eof;
    logic [31:0][6:0]   tx_eop_len;
    logic [31:0][511:0] tx_data;
    logic [31:0]        tx_ready;

    deparser dut (
        .clk_dp    (clk),
        .rst_dp_n  (rst_n),
        .phv_in    (phv_bus.dst),
        .pb_rd     (pb_bus.master),
        .tx_valid  (tx_valid),
        .tx_sof    (tx_sof),
        .tx_eof    (tx_eof),
        .tx_eop_len(tx_eop_len),
        .tx_data   (tx_data),
        .tx_ready  (tx_ready),
        .csr       (apb_bus.slave)
    );

    // ── APB tie-off ──────────────────────────────────────────────────
    assign apb_bus.psel    = 0;
    assign apb_bus.penable = 0;
    assign apb_bus.pwrite  = 0;
    assign apb_bus.paddr   = 0;
    assign apb_bus.pwdata  = 0;

    // ── All ports ready by default ───────────────────────────────────
    assign tx_ready = '1;

    // ── pb_rd slave model ────────────────────────────────────────────
    // Simple model: always ready, returns one cell then EOF
    logic [511:0] pb_cell_data;
    logic         pb_do_rsp;
    int           pb_rsp_delay;

    assign pb_bus.req_ready        = 1'b1;
    assign pb_bus.rsp_valid        = pb_do_rsp;
    assign pb_bus.rsp_data         = pb_cell_data;
    assign pb_bus.rsp_next_cell_id = '0;
    assign pb_bus.rsp_eof          = pb_do_rsp;  // single-cell packets

    // Drive rsp one cycle after req
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pb_do_rsp <= 0;
        end else begin
            pb_do_rsp <= pb_bus.req_valid;
        end
    end

    // ── PHV driver task ──────────────────────────────────────────────
    task automatic send_phv(
        input logic [4:0]            eg_port,
        input logic [CELL_ID_W-1:0]  cell_id,
        input logic [13:0]           pkt_len,
        input logic                  drop,
        input logic [PHV_BITS-1:0]   phv_data
    );
        phv_meta_t m;
        m          = '0;
        m.eg_port  = eg_port;
        m.cell_id  = cell_id;
        m.pkt_len  = pkt_len;
        m.drop     = drop;

        @(posedge clk);
        phv_bus.valid = 1'b1;
        phv_bus.data  = phv_data;
        phv_bus.meta  = m;
        // Wait for ready
        while (!phv_bus.ready) @(posedge clk);
        @(posedge clk);
        phv_bus.valid = 1'b0;
        phv_bus.data  = '0;
        phv_bus.meta  = '0;
    endtask

    // ── Helper: build a minimal PHV with Ethernet + IPv4 header ─────
    // Returns a PHV_BITS-wide vector with the relevant bytes set.
    function automatic logic [PHV_BITS-1:0] build_phv(
        input logic [47:0] dst_mac,
        input logic [47:0] src_mac,
        input logic [15:0] etype,
        input logic [7:0]  ttl,
        input logic [7:0]  proto,
        input logic [15:0] ip_csum,
        input logic [31:0] src_ip,
        input logic [31:0] dst_ip
    );
        logic [PHV_BITS-1:0] p;
        p = '0;
        // bytes 0-5: dst MAC
        p[PHV_BITS-1    -: 8] = dst_mac[47:40];
        p[PHV_BITS-9    -: 8] = dst_mac[39:32];
        p[PHV_BITS-17   -: 8] = dst_mac[31:24];
        p[PHV_BITS-25   -: 8] = dst_mac[23:16];
        p[PHV_BITS-33   -: 8] = dst_mac[15:8];
        p[PHV_BITS-41   -: 8] = dst_mac[7:0];
        // bytes 6-11: src MAC
        p[PHV_BITS-49   -: 8] = src_mac[47:40];
        p[PHV_BITS-57   -: 8] = src_mac[39:32];
        p[PHV_BITS-65   -: 8] = src_mac[31:24];
        p[PHV_BITS-73   -: 8] = src_mac[23:16];
        p[PHV_BITS-81   -: 8] = src_mac[15:8];
        p[PHV_BITS-89   -: 8] = src_mac[7:0];
        // bytes 12-13: EtherType
        p[PHV_BITS-97   -: 8] = etype[15:8];
        p[PHV_BITS-105  -: 8] = etype[7:0];
        // bytes 14-23: IPv4 header (IHL=5, DSCP=0, len=0, id=0, flags=0, frag=0)
        p[PHV_BITS-113  -: 8] = 8'h45; // version+IHL
        p[PHV_BITS-121  -: 8] = 8'h00; // DSCP/ECN
        p[PHV_BITS-129  -: 8] = 8'h00; // total len hi
        p[PHV_BITS-137  -: 8] = 8'h28; // total len lo (40)
        p[PHV_BITS-145  -: 8] = 8'h00; // id hi
        p[PHV_BITS-153  -: 8] = 8'h00; // id lo
        p[PHV_BITS-161  -: 8] = 8'h00; // flags/frag hi
        p[PHV_BITS-169  -: 8] = 8'h00; // frag lo
        // bytes 22-23: IPv4 checksum (at IPv4 offset 10-11 = frame byte 24-25)
        // Per spec: PHV bytes 22-23 = checksum? Let's use bytes 24-25 for csum
        // and bytes 26-27 for TTL+proto as stated in spec.
        // byte 22 = IPv4 TTL (frame byte 22 = IPv4 offset 8)
        // byte 23 = IPv4 proto (frame byte 23 = IPv4 offset 9)
        // byte 24-25 = IPv4 checksum (frame bytes 24-25 = IPv4 offset 10-11)
        // byte 26-27 per spec = TTL+proto — but that conflicts with standard layout.
        // The spec says PHV bytes 26-27 hold TTL+proto. We follow the spec.
        p[PHV_BITS-209  -: 8] = ttl;    // PHV byte 26 = TTL
        p[PHV_BITS-217  -: 8] = proto;  // PHV byte 27 = proto
        // bytes 24-25: checksum (PHV bytes 24-25)
        p[PHV_BITS-193  -: 8] = ip_csum[15:8];
        p[PHV_BITS-201  -: 8] = ip_csum[7:0];
        // bytes 30-33: src IP (PHV bytes 30-33)
        p[PHV_BITS-241  -: 8] = src_ip[31:24];
        p[PHV_BITS-249  -: 8] = src_ip[23:16];
        p[PHV_BITS-257  -: 8] = src_ip[15:8];
        p[PHV_BITS-265  -: 8] = src_ip[7:0];
        // bytes 34-37: dst IP (PHV bytes 34-37)
        p[PHV_BITS-273  -: 8] = dst_ip[31:24];
        p[PHV_BITS-281  -: 8] = dst_ip[23:16];
        p[PHV_BITS-289  -: 8] = dst_ip[15:8];
        p[PHV_BITS-297  -: 8] = dst_ip[7:0];
        return p;
    endfunction

    // ── RFC-1624 expected checksum helper ────────────────────────────
    function automatic logic [15:0] expected_csum_after_ttl_dec(
        input logic [15:0] old_csum,
        input logic [7:0]  old_ttl
    );
        logic [15:0] old_word, new_word;
        logic [16:0] tmp;
        old_word = {old_ttl,       8'h00};  // TTL in high byte, proto=0 for simplicity
        new_word = {old_ttl - 8'd1, 8'h00};
        tmp = {1'b0, ~old_csum} + {1'b0, ~old_word};
        tmp = {1'b0, tmp[15:0] + {15'b0, tmp[16]}} + {1'b0, new_word};
        return ~(tmp[15:0] + {15'b0, tmp[16]});
    endfunction

    // ── Test pass/fail counters ──────────────────────────────────────
    int pass_cnt, fail_cnt;

    task automatic check(input string name, input logic cond);
        if (cond) begin
            $display("PASS: %s", name);
            pass_cnt++;
        end else begin
            $display("FAIL: %s", name);
            fail_cnt++;
        end
    endtask

    // ── Wait for TX on a specific port ──────────────────────────────
    task automatic wait_tx(
        input  int           port,
        output logic [511:0] got_data,
        output logic         got_sof,
        output logic         got_eof,
        input  int           timeout_cycles
    );
        int cnt;
        got_data = '0;
        got_sof  = 0;
        got_eof  = 0;
        cnt = 0;
        while (cnt < timeout_cycles) begin
            @(posedge clk);
            #1;
            if (tx_valid[port]) begin
                got_data = tx_data[port];
                got_sof  = tx_sof[port];
                got_eof  = tx_eof[port];
                return;
            end
            cnt++;
        end
        $display("TIMEOUT waiting for TX on port %0d", port);
    endtask

    // ── Main test sequence ───────────────────────────────────────────
    initial begin
        pass_cnt = 0;
        fail_cnt = 0;

        // Init PHV bus
        phv_bus.valid = 0;
        phv_bus.data  = '0;
        phv_bus.meta  = '0;

        // Init pb cell data
        pb_cell_data = '0;

        // Wait for reset deassertion
        @(posedge rst_n);
        repeat(2) @(posedge clk);

        // ════════════════════════════════════════════════════════════
        // TC1: Send PHV with eg_port=2, verify TX appears on port 2
        //      and NOT on other ports.
        // ════════════════════════════════════════════════════════════
        begin
            logic [PHV_BITS-1:0] phv;
            logic [511:0]        got_data;
            logic                got_sof, got_eof;
            logic [47:0]         dst_mac, src_mac;

            dst_mac = 48'hAABBCCDDEEFF;
            src_mac = 48'h112233445566;

            // Build a non-IPv4 PHV (EtherType = ARP) to keep it simple
            phv = '0;
            // dst MAC bytes 0-5
            phv[PHV_BITS-1   -: 8] = dst_mac[47:40];
            phv[PHV_BITS-9   -: 8] = dst_mac[39:32];
            phv[PHV_BITS-17  -: 8] = dst_mac[31:24];
            phv[PHV_BITS-25  -: 8] = dst_mac[23:16];
            phv[PHV_BITS-33  -: 8] = dst_mac[15:8];
            phv[PHV_BITS-41  -: 8] = dst_mac[7:0];
            // src MAC bytes 6-11
            phv[PHV_BITS-49  -: 8] = src_mac[47:40];
            phv[PHV_BITS-57  -: 8] = src_mac[39:32];
            phv[PHV_BITS-65  -: 8] = src_mac[31:24];
            phv[PHV_BITS-73  -: 8] = src_mac[23:16];
            phv[PHV_BITS-81  -: 8] = src_mac[15:8];
            phv[PHV_BITS-89  -: 8] = src_mac[7:0];
            // EtherType = ARP (0x0806) bytes 12-13
            phv[PHV_BITS-97  -: 8] = 8'h08;
            phv[PHV_BITS-105 -: 8] = 8'h06;

            // Set pb cell data to something recognisable
            pb_cell_data = 512'hDEAD_BEEF;

            send_phv(5'd2, 20'd100, 14'd64, 1'b0, phv);

            wait_tx(2, got_data, got_sof, got_eof, 30);

            check("TC1: tx_valid on port 2",    tx_valid[2] === 1'b1);
            check("TC1: tx_sof on port 2",      got_sof    === 1'b1);
            check("TC1: no tx_valid on port 0", tx_valid[0] === 1'b0);
            check("TC1: no tx_valid on port 1", tx_valid[1] === 1'b0);
            // Verify dst MAC reconstructed in output cell
            check("TC1: dst MAC byte 0",
                got_data[511:504] === dst_mac[47:40]);
            check("TC1: dst MAC byte 5",
                got_data[471:464] === dst_mac[7:0]);
            check("TC1: src MAC byte 0",
                got_data[463:456] === src_mac[47:40]);
            check("TC1: EtherType hi",
                got_data[415:408] === 8'h08);
            check("TC1: EtherType lo",
                got_data[407:400] === 8'h06);
        end

        repeat(5) @(posedge clk);

        // ════════════════════════════════════════════════════════════
        // TC2: TTL decrement -> checksum update
        // ════════════════════════════════════════════════════════════
        begin
            logic [PHV_BITS-1:0] phv;
            logic [511:0]        got_data;
            logic                got_sof, got_eof;
            logic [7:0]          ttl_in;
            logic [15:0]         csum_in;
            logic [15:0]         csum_exp;
            logic [7:0]          got_ttl;
            logic [15:0]         got_csum;

            ttl_in  = 8'd64;
            csum_in = 16'hB861;  // example valid checksum

            phv = build_phv(
                48'h010203040506,  // dst
                48'h0A0B0C0D0E0F,  // src
                ETYPE_IPV4,
                ttl_in,
                PROTO_TCP,
                csum_in,
                32'hC0A80001,      // 192.168.0.1
                32'hC0A80002       // 192.168.0.2
            );

            pb_cell_data = '0;

            send_phv(5'd5, 20'd200, 14'd40, 1'b0, phv);

            wait_tx(5, got_data, got_sof, got_eof, 30);

            got_ttl  = got_data[511-26*8 -: 8];
            got_csum = {got_data[511-24*8 -: 8], got_data[511-25*8 -: 8]};

            // Expected: TTL decremented by 1
            check("TC2: TTL decremented", got_ttl === (ttl_in - 8'd1));

            // Expected checksum via RFC-1624
            // old_word = {ttl_in, proto} = {64, TCP=6}
            // new_word = {ttl_in-1, proto}
            begin
                logic [15:0] old_word, new_word, exp_c;
                logic [16:0] tmp;
                old_word = {ttl_in,        PROTO_TCP};
                new_word = {ttl_in - 8'd1, PROTO_TCP};
                tmp = {1'b0, ~csum_in} + {1'b0, ~old_word};
                tmp = {1'b0, tmp[15:0] + {15'b0, tmp[16]}} + {1'b0, new_word};
                exp_c = ~(tmp[15:0] + {15'b0, tmp[16]});
                check("TC2: checksum updated (RFC-1624)", got_csum === exp_c);
            end

            // Verify EtherType is IPv4
            check("TC2: EtherType IPv4",
                {got_data[415:408], got_data[407:400]} === ETYPE_IPV4);
        end

        repeat(5) @(posedge clk);

        // ════════════════════════════════════════════════════════════
        // TC3: drop flag set -> no TX output on any port
        // ════════════════════════════════════════════════════════════
        begin
            logic [PHV_BITS-1:0] phv;
            logic                saw_tx;
            int                  wait_cnt;

            phv = '0;
            phv[PHV_BITS-97  -: 8] = 8'h08;
            phv[PHV_BITS-105 -: 8] = 8'h00;

            pb_cell_data = 512'hCAFEBABE;

            send_phv(5'd7, 20'd300, 14'd64, 1'b1 /*drop*/, phv);

            // Wait 20 cycles and confirm no TX on port 7
            saw_tx   = 0;
            wait_cnt = 0;
            while (wait_cnt < 20) begin
                @(posedge clk);
                #1;
                if (tx_valid[7]) saw_tx = 1;
                wait_cnt++;
            end
            check("TC3: no TX when drop=1", saw_tx === 1'b0);
        end

        repeat(5) @(posedge clk);

        // ── Summary ─────────────────────────────────────────────────
        $display("─────────────────────────────────────");
        $display("Results: %0d passed, %0d failed", pass_cnt, fail_cnt);
        if (fail_cnt == 0)
            $display("ALL TESTS PASSED");
        else
            $display("SOME TESTS FAILED");
        $display("─────────────────────────────────────");
        $finish;
    end

    // ── Timeout watchdog ─────────────────────────────────────────────
    initial begin
        #100000;
        $display("WATCHDOG TIMEOUT");
        $finish;
    end

endmodule
