// mac_rx_arb.sv
// Round-robin arbiter: mux 32 RX ports into a single mac_rx_if

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module mac_rx_arb
    import rv_p4_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // 32 RX port inputs (flat)
    input  logic [31:0]        rx_valid,
    input  logic [31:0]        rx_sof,
    input  logic [31:0]        rx_eof,
    input  logic [31:0][6:0]   rx_eop_len,
    input  logic [31:0][511:0] rx_data,
    output logic [31:0]        rx_ready,

    // Single output cell stream
    mac_rx_if.src out
);

    // Round-robin pointer
    logic [4:0] rr_ptr;

    // Grant: find next valid port starting from rr_ptr
    logic [4:0]  grant;
    logic        any_valid;

    // Round-robin priority: scan from rr_ptr wrapping around
    logic [4:0] scan_idx;
    always_comb begin
        grant     = rr_ptr;
        any_valid = 1'b0;
        for (int i = 0; i < NUM_PORTS; i++) begin
            scan_idx = 5'((int'(rr_ptr) + i) % NUM_PORTS);
            if (!any_valid && rx_valid[scan_idx]) begin
                grant     = scan_idx;
                any_valid = 1'b1;
            end
        end
    end

    // Advance pointer when we accept a cell
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            rr_ptr <= '0;
        else if (any_valid && out.ready)
            rr_ptr <= grant + 5'd1;
    end

    // Output mux
    assign out.valid   = any_valid;
    assign out.port    = grant;
    assign out.sof     = rx_sof[grant];
    assign out.eof     = rx_eof[grant];
    assign out.eop_len = rx_eop_len[grant];
    assign out.data    = rx_data[grant];

    // Back-pressure: only assert ready to the granted port
    generate
        for (genvar p = 0; p < NUM_PORTS; p++) begin : gen_ready
            assign rx_ready[p] = (grant == 5'(p)) && out.ready && any_valid;
        end
    endgenerate

endmodule
