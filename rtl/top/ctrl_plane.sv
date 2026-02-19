// ctrl_plane.sv
// Control plane stub — ties all outputs to 0

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module ctrl_plane
    import rv_p4_pkg::*;
(
    input  logic clk_ctrl,
    input  logic rst_ctrl_n,
    input  logic clk_cpu,
    input  logic rst_cpu_n,
    input  logic pcie_clk,

    input  logic [255:0] pcie_rx_data,
    output logic [255:0] pcie_tx_data,
    input  logic         pcie_rx_valid,
    output logic         pcie_tx_valid,

    // APB master — drive all slave slots
    apb_if.master apb [16],

    // TUE request interface
    tue_req_if.master tue_req,

    // JTAG
    input  logic tck,
    input  logic tms,
    input  logic tdi,
    output logic tdo
);

    // Tie all outputs to 0
    assign pcie_tx_data  = '0;
    assign pcie_tx_valid = 1'b0;
    assign tdo           = 1'b0;

    assign tue_req.valid = 1'b0;
    assign tue_req.req   = '0;

    generate
        for (genvar i = 0; i < 16; i++) begin : gen_apb
            assign apb[i].psel    = 1'b0;
            assign apb[i].penable = 1'b0;
            assign apb[i].pwrite  = 1'b0;
            assign apb[i].paddr   = '0;
            assign apb[i].pwdata  = '0;
        end
    endgenerate

endmodule
