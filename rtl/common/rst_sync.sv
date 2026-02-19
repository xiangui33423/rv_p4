// rst_sync.sv — 复位同步器（异步复位，同步释放，2-FF）

module rst_sync (
    input  logic clk,
    input  logic rst_async_n,
    output logic rst_sync_n
);
    logic ff1, ff2;
    (* ASYNC_REG = "TRUE" *)
    always_ff @(posedge clk or negedge rst_async_n) begin
        if (!rst_async_n) {ff2, ff1} <= 2'b00;
        else              {ff2, ff1} <= {ff1, 1'b1};
    end
    assign rst_sync_n = ff2;
endmodule
