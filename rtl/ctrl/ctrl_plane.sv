// ctrl_plane.sv
// 控制面顶层：香山核封装 + PCIe + APB 主控
// 香山核运行 C 固件，通过 MMIO 访问 TUE/CSR

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module ctrl_plane
    import rv_p4_pkg::*;
(
    input  logic clk_ctrl,
    input  logic rst_ctrl_n,
    input  logic clk_cpu,
    input  logic rst_cpu_n,

    // PCIe（固件加载 / 管理接口）
    input  logic        pcie_clk,
    input  logic [255:0] pcie_rx_data,
    output logic [255:0] pcie_tx_data,
    input  logic        pcie_rx_valid,
    output logic        pcie_tx_valid,

    // APB 主接口（驱动所有从设备）
    apb_if.master apb [16],

    // TUE 请求接口
    tue_req_if.master tue_req,

    // JTAG
    input  logic tck, tms, tdi,
    output logic tdo
);

    // ─────────────────────────────────────────
    // 内部 APB 总线仲裁
    // 香山核通过 MMIO 地址空间访问各模块 CSR
    // 地址映射：
    //   0xA000_0000 - 0xA000_0FFF  Parser CSR    (apb[0])
    //   0xA000_1000 - 0xA000_1FFF  MAU CSR       (apb[1])
    //   0xA000_2000 - 0xA000_2FFF  TM CSR        (apb[2])
    //   0xA000_3000 - 0xA000_3FFF  TUE CSR       (apb[3])
    //   0xA000_4000 - 0xA000_4FFF  PKT_BUF CSR   (apb[4])
    // ─────────────────────────────────────────

    // CPU MMIO 总线（来自香山核 TileLink → AXI → APB 桥）
    logic        cpu_apb_psel;
    logic        cpu_apb_penable;
    logic        cpu_apb_pwrite;
    logic [19:0] cpu_apb_paddr;   // 20b 地址（1MB 空间）
    logic [31:0] cpu_apb_pwdata;
    logic [31:0] cpu_apb_prdata;
    logic        cpu_apb_pready;

    // APB 地址译码（按高 4b 选从设备）
    logic [3:0] apb_sel_idx;
    assign apb_sel_idx = cpu_apb_paddr[15:12];

    // 驱动 APB 从设备
    always_comb begin
        for (int i = 0; i < 16; i++) begin
            apb[i].psel    = cpu_apb_psel && (apb_sel_idx == 4'(i));
            apb[i].penable = cpu_apb_penable;
            apb[i].pwrite  = cpu_apb_pwrite;
            apb[i].paddr   = cpu_apb_paddr[11:0];
            apb[i].pwdata  = cpu_apb_pwdata;
        end
        // 读数据回选
        cpu_apb_prdata = apb[apb_sel_idx].prdata;
        cpu_apb_pready = apb[apb_sel_idx].pready;
    end

    // ─────────────────────────────────────────
    // 香山核（XiangShan Nanhu）封装
    // 实际使用香山 Chisel 生成的 Verilog 黑盒
    // 此处为接口占位
    // ─────────────────────────────────────────
    // 香山核通过 TileLink 访问片上资源
    // TileLink → AXI4 → APB 桥（标准 IP）

    // 黑盒声明（香山核由 Chisel 生成）
    xiangshan_nanhu_core u_cpu (
        .clock          (clk_cpu),
        .reset          (~rst_cpu_n),

        // MMIO 接口（AXI4-Lite，连接到 APB 桥）
        .mmio_awaddr    (cpu_apb_paddr),
        .mmio_awvalid   (cpu_apb_psel),
        .mmio_wdata     (cpu_apb_pwdata),
        .mmio_wvalid    (cpu_apb_penable),
        .mmio_wstrb     (4'hF),
        .mmio_araddr    (cpu_apb_paddr),
        .mmio_arvalid   (cpu_apb_psel & ~cpu_apb_pwrite),
        .mmio_rdata     (cpu_apb_prdata),
        .mmio_rvalid    (cpu_apb_pready),
        .mmio_bready    (1'b1),

        // JTAG
        .io_jtag_TCK    (tck),
        .io_jtag_TMS    (tms),
        .io_jtag_TDI    (tdi),
        .io_jtag_TDO    (tdo)
    );

    // ─────────────────────────────────────────
    // PCIe DMA（固件加载）
    // 简化：PCIe 写请求直接转发到 APB 总线
    // 实际需要 PCIe IP + DMA 引擎
    // ─────────────────────────────────────────
    logic        pcie_wr_en;
    logic [19:0] pcie_wr_addr;
    logic [31:0] pcie_wr_data;

    // PCIe 接收解包（简化协议）
    always_ff @(posedge pcie_clk or negedge rst_ctrl_n) begin
        if (!rst_ctrl_n) begin
            pcie_wr_en   <= 1'b0;
            pcie_wr_addr <= '0;
            pcie_wr_data <= '0;
            pcie_tx_valid<= 1'b0;
            pcie_tx_data <= '0;
        end else if (pcie_rx_valid) begin
            // 简化：前 20b 为地址，后 32b 为数据
            pcie_wr_en   <= pcie_rx_data[255];    // bit255 = write enable
            pcie_wr_addr <= pcie_rx_data[254:235]; // [254:235] = addr
            pcie_wr_data <= pcie_rx_data[31:0];    // [31:0] = data
            // 回应
            pcie_tx_valid<= 1'b1;
            pcie_tx_data <= {224'b0, 32'hDEAD_BEEF}; // ACK
        end else begin
            pcie_wr_en   <= 1'b0;
            pcie_tx_valid<= 1'b0;
        end
    end

    // TUE 请求（由 CPU 通过 APB 写 TUE 寄存器触发，tue.sv 内部处理）
    // tue_req 接口由 TUE 模块直接从 APB 读取，此处不额外驱动
    assign tue_req.valid = 1'b0;
    assign tue_req.req   = '0;

endmodule

// ─────────────────────────────────────────────
// 香山核黑盒声明（Chisel 生成的 Verilog 顶层）
// ─────────────────────────────────────────────
module xiangshan_nanhu_core (
    input  logic        clock,
    input  logic        reset,
    // MMIO AXI4-Lite（简化）
    input  logic [19:0] mmio_awaddr,
    input  logic        mmio_awvalid,
    input  logic [31:0] mmio_wdata,
    input  logic        mmio_wvalid,
    input  logic [3:0]  mmio_wstrb,
    input  logic [19:0] mmio_araddr,
    input  logic        mmio_arvalid,
    output logic [31:0] mmio_rdata,
    output logic        mmio_rvalid,
    input  logic        mmio_bready,
    // JTAG
    input  logic        io_jtag_TCK,
    input  logic        io_jtag_TMS,
    input  logic        io_jtag_TDI,
    output logic        io_jtag_TDO
);
    // 黑盒：由香山 Chisel 工程生成
    // 实际综合时链接 XiangShan/build/XSTop.v
    assign mmio_rdata  = '0;
    assign mmio_rvalid = 1'b0;
    assign io_jtag_TDO = 1'b0;
endmodule
