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
    output logic tdo,

    // Co-simulation backdoor: TUE APB override for apb[2] (tie to 0 in production)
    input  logic [11:0] tb_tue_paddr,
    input  logic [31:0] tb_tue_pwdata,
    input  logic        tb_tue_psel,
    input  logic        tb_tue_penable,
    input  logic        tb_tue_pwrite,
    output logic [31:0] tb_tue_prdata,
    output logic        tb_tue_pready
);

    // Tie all outputs to 0
    assign pcie_tx_data  = '0;
    assign pcie_tx_valid = 1'b0;
    assign tdo           = 1'b0;

    assign tue_req.valid = 1'b0;
    assign tue_req.req   = '0;

    // APB slot 2 driven from cosim TUE backdoor
    assign apb[2].psel    = tb_tue_psel;
    assign apb[2].penable = tb_tue_penable;
    assign apb[2].pwrite  = tb_tue_pwrite;
    assign apb[2].paddr   = tb_tue_paddr;
    assign apb[2].pwdata  = tb_tue_pwdata;
    assign tb_tue_prdata  = apb[2].prdata;
    assign tb_tue_pready  = apb[2].pready;

    // All other APB slots tied to 0
    generate
        for (genvar i = 0; i < 16; i++) begin : gen_apb
            if (i != 2) begin : gen_other
                assign apb[i].psel    = 1'b0;
                assign apb[i].penable = 1'b0;
                assign apb[i].pwrite  = 1'b0;
                assign apb[i].paddr   = '0;
                assign apb[i].pwdata  = '0;
            end
        end
    endgenerate

endmodule
