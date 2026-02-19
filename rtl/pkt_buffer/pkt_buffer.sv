// pkt_buffer.sv
// 共享包缓冲管理器
// 64MiB / 64B cell = 1M cells，cell 链表管理
// 3 端口：Parser 写入 / TM 读取 / Deparser 读取

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module pkt_buffer
    import rv_p4_pkg::*;
(
    input  logic clk_dp,
    input  logic rst_dp_n,

    // Port 0: Parser 写入
    pb_wr_if.dst   wr,

    // Port 1: TM 读取（TX 调度）
    pb_rd_if.slave rd_tm,

    // Port 2: Deparser 读取（payload）
    pb_rd_if.slave rd_dp,

    // Cell 分配/释放
    cell_alloc_if.allocator alloc
);

    // ─────────────────────────────────────────
    // Cell 数据 SRAM（综合时映射到片上 SRAM）
    // 实际 64MiB 需要外部 SRAM 宏，此处用 logic 数组建模
    // ─────────────────────────────────────────
    // 为节省仿真内存，仅声明接口，实际综合时替换为 SRAM 宏
    logic [511:0] cell_data [NUM_CELLS];  // 64B × 1M

    // Cell 链表：每个 cell 存储下一 cell ID
    logic [CELL_ID_W-1:0] cell_next [NUM_CELLS];
    logic                  cell_eof  [NUM_CELLS]; // 是否为帧末尾 cell

    // ─────────────────────────────────────────
    // Free List（空闲 cell 管理）
    // 使用 FIFO 实现，初始化时填入所有 cell ID
    // ─────────────────────────────────────────
    localparam int FL_DEPTH = NUM_CELLS;
    localparam int FL_PTR_W = CELL_ID_W;

    logic [CELL_ID_W-1:0] free_list [FL_DEPTH];
    logic [FL_PTR_W-1:0]  fl_head;   // 出队指针（分配）
    logic [FL_PTR_W-1:0]  fl_tail;   // 入队指针（释放）
    logic [FL_PTR_W:0]    fl_count;  // 空闲 cell 数

    // 初始化：将所有 cell 加入 free list
    // 实际综合时通过 ROM 初始化或上电序列完成
    initial begin
        for (int i = 0; i < FL_DEPTH; i++)
            free_list[i] = CELL_ID_W'(i);
        fl_head  = '0;
        fl_tail  = '0;
        fl_count = (FL_PTR_W+1)'(FL_DEPTH);
    end

    // ─────────────────────────────────────────
    // Cell 分配（alloc）
    // ─────────────────────────────────────────
    assign alloc.alloc_valid = (fl_count > 0);
    assign alloc.alloc_empty = (fl_count == 0);
    assign alloc.alloc_id    = free_list[fl_head];

    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            fl_head  <= '0;
            fl_tail  <= '0;
            fl_count <= (FL_PTR_W+1)'(FL_DEPTH);
        end else begin
            // 分配
            if (alloc.alloc_req && alloc.alloc_valid) begin
                fl_head  <= fl_head + 1'b1;
                fl_count <= fl_count - 1'b1;
            end
            // 释放
            if (alloc.free_req) begin
                free_list[fl_tail] <= alloc.free_id;
                fl_tail  <= fl_tail + 1'b1;
                fl_count <= fl_count + 1'b1;
            end
        end
    end

    // ─────────────────────────────────────────
    // Port 0：写入（Parser → cell_data）
    // ─────────────────────────────────────────
    assign wr.ready = 1'b1; // SRAM 写无背压（假设 SRAM 单周期写）

    always_ff @(posedge clk_dp) begin
        if (wr.valid && wr.ready) begin
            cell_data[wr.cell_id] <= wr.data;
            cell_eof[wr.cell_id]  <= wr.eof;
            // next cell ID 由 Parser 在下一 alloc 时填入
            // 此处简化：next = cell_id + 1（实际由 Parser 写链表）
            if (!wr.eof)
                cell_next[wr.cell_id] <= wr.cell_id + CELL_ID_W'(1);
            else
                cell_next[wr.cell_id] <= '1; // 0xFFFFF = 链表末尾
        end
    end

    // ─────────────────────────────────────────
    // Port 1：TM 读取（1 cycle 延迟）
    // ─────────────────────────────────────────
    assign rd_tm.req_ready = 1'b1;

    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            rd_tm.rsp_valid        <= 1'b0;
            rd_tm.rsp_data         <= '0;
            rd_tm.rsp_next_cell_id <= '0;
            rd_tm.rsp_eof          <= 1'b0;
        end else begin
            rd_tm.rsp_valid        <= rd_tm.req_valid;
            rd_tm.rsp_data         <= cell_data[rd_tm.req_cell_id];
            rd_tm.rsp_next_cell_id <= cell_next[rd_tm.req_cell_id];
            rd_tm.rsp_eof          <= cell_eof[rd_tm.req_cell_id];
        end
    end

    // ─────────────────────────────────────────
    // Port 2：Deparser 读取（1 cycle 延迟）
    // ─────────────────────────────────────────
    assign rd_dp.req_ready = 1'b1;

    always_ff @(posedge clk_dp or negedge rst_dp_n) begin
        if (!rst_dp_n) begin
            rd_dp.rsp_valid        <= 1'b0;
            rd_dp.rsp_data         <= '0;
            rd_dp.rsp_next_cell_id <= '0;
            rd_dp.rsp_eof          <= 1'b0;
        end else begin
            rd_dp.rsp_valid        <= rd_dp.req_valid;
            rd_dp.rsp_data         <= cell_data[rd_dp.req_cell_id];
            rd_dp.rsp_next_cell_id <= cell_next[rd_dp.req_cell_id];
            rd_dp.rsp_eof          <= cell_eof[rd_dp.req_cell_id];
        end
    end

endmodule
