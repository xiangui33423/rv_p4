// rst_sync.sv
// 2-FF reset synchronizer
// Async assert, sync deassert

module rst_sync (
    input  logic clk,
    input  logic rst_async_n,
    output logic rst_sync_n
);
    logic ff1;

    always_ff @(posedge clk or negedge rst_async_n) begin
        if (!rst_async_n) begin
            ff1        <= 1'b0;
            rst_sync_n <= 1'b0;
        end else begin
            ff1        <= 1'b1;
            rst_sync_n <= ff1;
        end
    end

endmodule
