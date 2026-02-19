// tue.sv
// Table Update Engine — 原子更新 TCAM/SRAM
// shadow write + pointer swap，保证数据面不中断
// APB 从端接收控制面写请求，跨时钟域同步到 clk_dp

`include "rv_p4_pkg.sv"
`include "rv_p4_if.sv"

module tue
    import rv_p4_pkg::*;
(
    input  logic clk_ctrl,
    input  logic rst_ctrl_n,
    input  logic clk_dp,

    // APB 从端（香山核 MMIO）
    apb_if.slave csr,

    // TUE 请求（来自 ctrl_plane，可选直接接口）
    tue_req_if.slave req,

    // → MAU 配置（广播到所有级）
    mau_cfg_if.driver mau_cfg [NUM_MAU_STAGES],

    // → Parser FSM 更新
    output logic                       parser_wr_en,
    output logic [7:0]                 parser_wr_addr,
    output logic [PARSER_TCAM_WIDTH-1:0] parser_wr_data
);

    // ─────────────────────────────────────────
    // APB 寄存器文件（clk_ctrl 域）
    // ─────────────────────────────────────────
    logic [1:0]                    reg_cmd;        // TUE_INSERT/DELETE/MODIFY/FLUSH
    logic [4:0]                    reg_stage;
    logic [15:0]                   reg_table_id;
    logic [MAU_TCAM_KEY_W-1:0]     reg_key;        // 16 × 32b 寄存器
    logic [MAU_TCAM_KEY_W-1:0]     reg_mask;
    logic [15:0]                   reg_action_id;
    logic [95:0]                   reg_action_params; // 3 × 32b
    logic                          reg_commit;     // 写 1 触发事务
    logic [1:0]                    reg_status;     // 0=idle,1=busy,2=done,3=err

    // APB 写
    always_ff @(posedge clk_ctrl or negedge rst_ctrl_n) begin
        if (!rst_ctrl_n) begin
            reg_cmd          <= '0;
            reg_stage        <= '0;
            reg_table_id     <= '0;
            reg_key          <= '0;
            reg_mask         <= '0;
            reg_action_id    <= '0;
            reg_action_params<= '0;
            reg_commit       <= 1'b0;
        end else begin
            reg_commit <= 1'b0; // 自清
            if (csr.psel && csr.penable && csr.pwrite) begin
                case (csr.paddr)
                    TUE_REG_CMD:      reg_cmd       <= csr.pwdata[1:0];
                    TUE_REG_TABLE_ID: reg_table_id  <= csr.pwdata[15:0];
                    TUE_REG_STAGE:    reg_stage      <= csr.pwdata[4:0];
                    // key[31:0] ~ key[511:480]：16 个连续寄存器
                    default: begin
                        if (csr.paddr >= TUE_REG_KEY_0 &&
                            csr.paddr <= TUE_REG_KEY_0 + 12'h3C) begin
                            automatic int widx = (int'(csr.paddr) - int'(TUE_REG_KEY_0)) >> 2;
                            reg_key[widx*32 +: 32] <= csr.pwdata;
                        end
                        if (csr.paddr >= TUE_REG_MASK_0 &&
                            csr.paddr <= TUE_REG_MASK_0 + 12'h3C) begin
                            automatic int widx = (int'(csr.paddr) - int'(TUE_REG_MASK_0)) >> 2;
                            reg_mask[widx*32 +: 32] <= csr.pwdata;
                        end
                        if (csr.paddr == TUE_REG_ACTION_ID)
                            reg_action_id <= csr.pwdata[15:0];
                        if (csr.paddr == TUE_REG_ACTION_P0)
                            reg_action_params[31:0]  <= csr.pwdata;
                        if (csr.paddr == TUE_REG_ACTION_P1)
                            reg_action_params[63:32] <= csr.pwdata;
                        if (csr.paddr == TUE_REG_ACTION_P2)
                            reg_action_params[95:64] <= csr.pwdata;
                        if (csr.paddr == TUE_REG_COMMIT)
                            reg_commit <= csr.pwdata[0];
                    end
                endcase
            end
        end
    end

    // APB 读
    always_comb begin
        csr.prdata  = '0;
        csr.pslverr = 1'b0;
        case (csr.paddr)
            TUE_REG_STATUS: csr.prdata = {30'b0, reg_status};
            TUE_REG_STAGE:  csr.prdata = {27'b0, reg_stage};
            default:        csr.prdata = '0;
        endcase
    end
    assign csr.pready = 1'b1;

    // ─────────────────────────────────────────
    // 事务状态机（clk_ctrl 域）
    // ─────────────────────────────────────────
    typedef enum logic [1:0] {
        TS_IDLE,
        TS_WAIT_DRAIN,  // 等待 dp 流水线排空（计数 32 cycles）
        TS_APPLY,       // 写入 shadow，触发 pointer swap
        TS_DONE
    } tue_state_t;

    tue_state_t  ts;
    logic [5:0]  drain_cnt;  // 等待 32 cycles

    // 跨时钟域：ctrl → dp 的写使能脉冲（2-FF 同步）
    logic apply_pulse_ctrl;
    logic apply_pulse_dp_ff1, apply_pulse_dp;

    always_ff @(posedge clk_ctrl or negedge rst_ctrl_n) begin
        if (!rst_ctrl_n) begin
            ts             <= TS_IDLE;
            drain_cnt      <= '0;
            reg_status     <= 2'b00;
            apply_pulse_ctrl <= 1'b0;
        end else begin
            apply_pulse_ctrl <= 1'b0;
            case (ts)
                TS_IDLE: begin
                    if (reg_commit) begin
                        reg_commit <= 1'b0;  // 自清
                        ts         <= TS_WAIT_DRAIN;
                        drain_cnt  <= 6'd32;
                        reg_status <= 2'b01; // busy
                    end
                end
                TS_WAIT_DRAIN: begin
                    if (drain_cnt == 0) begin
                        ts               <= TS_APPLY;
                        apply_pulse_ctrl <= 1'b1;
                        // 提前锁存，确保 dp 域信号在 apply_pulse_dp 触发前已稳定
                        dp_stage         <= reg_stage;
                        dp_key           <= reg_key;
                        dp_mask          <= reg_mask;
                        dp_action_id     <= reg_action_id;
                        dp_action_params <= reg_action_params;
                        dp_cmd           <= reg_cmd;
                    end else
                        drain_cnt <= drain_cnt - 1'b1;
                end
                TS_APPLY: begin
                    ts         <= TS_DONE;
                    reg_status <= 2'b10; // done
                end
                TS_DONE: begin
                    ts         <= TS_IDLE;
                    reg_status <= 2'b00;
                end
            endcase
        end
    end

    // 2-FF 同步器：ctrl → dp
    always_ff @(posedge clk_dp) begin
        apply_pulse_dp_ff1 <= apply_pulse_ctrl;
        apply_pulse_dp     <= apply_pulse_dp_ff1;
    end

    // ─────────────────────────────────────────
    // 写入 MAU 配置（clk_dp 域，apply_pulse_dp 触发）
    // ─────────────────────────────────────────
    // 寄存配置值（跨时钟域，在 apply_pulse_dp 时已稳定）
    logic [4:0]                  dp_stage;
    logic [MAU_TCAM_KEY_W-1:0]   dp_key, dp_mask;
    logic [15:0]                 dp_action_id;
    logic [95:0]                 dp_action_params;
    logic [1:0]                  dp_cmd;

    // dp 域信号已在 TS_WAIT_DRAIN 末尾锁存，无需额外 always_ff

    // 广播到对应 MAU 级（generate 展开，避免 Verilator 动态 interface 索引限制）
    generate
        for (genvar i = 0; i < NUM_MAU_STAGES; i++) begin : gen_mau_cfg
            assign mau_cfg[i].tcam_wr_en     = apply_pulse_dp && (dp_stage == 5'(i))
                                               && (dp_cmd != 2'b11);
            assign mau_cfg[i].tcam_wr_addr   = 11'(reg_table_id[10:0]);
            assign mau_cfg[i].tcam_wr_key    = dp_key;
            assign mau_cfg[i].tcam_wr_mask   = dp_mask;
            assign mau_cfg[i].tcam_action_id = dp_action_id;
            assign mau_cfg[i].tcam_action_ptr= reg_table_id;
            assign mau_cfg[i].tcam_wr_valid  = (dp_cmd == 2'b00);
            assign mau_cfg[i].asram_wr_en    = apply_pulse_dp && (dp_stage == 5'(i));
            assign mau_cfg[i].asram_wr_addr  = reg_table_id;
            assign mau_cfg[i].asram_wr_data  = {dp_action_id, 16'b0, dp_action_params};
        end
    endgenerate

    // Parser FSM 更新（stage == 5'h1F 保留给 Parser）
    assign parser_wr_en   = apply_pulse_dp && (dp_stage == 5'h1F);
    assign parser_wr_addr = dp_key[7:0];
    assign parser_wr_data = {{(PARSER_TCAM_WIDTH-MAU_TCAM_KEY_W){1'b0}}, dp_key};

    // req 接口（直接接口，优先级低于 APB）
    assign req.ready = (ts == TS_IDLE);
    assign req.done  = (ts == TS_DONE);
    assign req.error = 1'b0;

endmodule
