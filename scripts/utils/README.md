# 工具和实用脚本

其他实用工具和配置文件。

## 📁 文件

| 文件 | 功能 |
|------|------|
| `simulate_flow.py` | 完整设计流程模拟（无需实际工具） |
| `gen_xs_blackbox.py` | 香山黑盒生成器 |
| `rv_p4.sdc` | 设计约束文件（SDC 格式） |

---

## 🎯 simulate_flow.py

**功能**：模拟完整 ASIC 设计流程（演示用）

**用途**：
- 无需安装 Yosys、OpenROAD 等工具
- 快速演示设计流程
- 生成预期的输出结果

**运行**：

```bash
python3 simulate_flow.py
```

**输出**：

```
═══════════════════════════════════════════════════════════
  RV-P4 ASIC Design Flow Simulation
═══════════════════════════════════════════════════════════

[1/8] SYNTHESIS
----------------------------------------------
Input:  rtl/**/*.sv
Tool:   Yosys (ISC)
Status: SIMULATED ✓
Output: synthesis_results/rv_p4_netlist.v (12.5 KB)
        synthesis_results/area_report.txt

[2/8] PLACEMENT
----------------------------------------------
Input:  rv_p4_netlist.v
Tool:   OpenROAD (Apache 2.0)
Status: SIMULATED ✓
Output: implementation/place_route/rv_p4_placed.def

...

═══════════════════════════════════════════════════════════
✓ All stages completed successfully!
═══════════════════════════════════════════════════════════

Summary Report:
  Synthesis:     ✓ Gate-level netlist generated
  Placement:     ✓ Module placement optimized
  Routing:       ✓ Signal routing completed
  STA:           ✓ Timing closure achieved (+8 ps)
  Power:         ✓ 156 mW @ 27°C
  DRC:           ✓ 0 violations
  LVS:           ✓ Layout matches schematic
  GDS:           ✓ GDS-II file generated

Die Size:        9.47 × 9.47 mm
Core Area:       46.5 mm²
Total Area:      89.8 mm²
Utilization:     68.5%
Frequency:       1.6 GHz
Power:           156 mW

Ready for tapeout! 🎉
```

**特点**：
- ✓ 完整的流程演示
- ✓ 模拟输出文件生成
- ✓ 详细的进度报告
- ✓ 最终汇总信息

**何时使用**：
- 演示设计流程给非技术人员
- 生成演示报告
- 工具不可用时的备选方案
- 验证流程配置

---

## 🔧 gen_xs_blackbox.py

**功能**：为香山 RISC-V 核生成黑盒模型

**用途**：
- 在 RTL 仿真中使用香山黑盒（加快仿真）
- 生成 Verilog 接口定义
- 隐藏香山内部实现细节

**运行**：

```bash
python3 gen_xs_blackbox.py
```

**生成的黑盒接口**：

```verilog
module xiangshan_blackbox (
    // Clock and Reset
    input clk,
    input rst_n,

    // Memory Interface (AXI)
    output [63:0] axi_awaddr,
    output [7:0]  axi_awlen,
    output [2:0]  axi_awsize,
    output        axi_awvalid,
    input         axi_awready,

    output [63:0] axi_wdata,
    output [7:0]  axi_wstrb,
    output        axi_wlast,
    output        axi_wvalid,
    input         axi_wready,

    input [63:0]  axi_rdata,
    input [1:0]   axi_rresp,
    input         axi_rlast,
    input         axi_rvalid,
    output        axi_rready,

    // Control Plane Interface
    output [31:0] ctrl_addr,
    output [31:0] ctrl_data,
    output [3:0]  ctrl_mask,
    output        ctrl_valid,
    input         ctrl_ready,

    // Interrupts
    input [31:0]  irq
);

    // 黑盒实现隐藏香山内部逻辑
    // 仅保留接口定义供集成使用

endmodule
```

**输出**：
- `xiangshan_blackbox.v` - 黑盒接口文件
- `xiangshan_blackbox.sv` - SystemVerilog 版本

**使用场景**：
- 集成验证（无需大型香山数据库）
- 分层设计（控制面和数据面分离）
- 仿真加速

---

## ⚙️ rv_p4.sdc

**功能**：设计约束文件（Synopsys Design Constraints）

**内容**：定义设计的时序、功率和其他约束

### 时钟定义

```tcl
# 主数据平面时钟
create_clock -name clk_dp -period 0.625 [get_ports clk_dp]

# 控制平面时钟
create_clock -name clk_cpu -period 0.667 [get_ports clk_cpu]

# 控制器时钟
create_clock -name clk_ctrl -period 5.0 [get_ports clk_ctrl]

# MAC 时钟
create_clock -name clk_mac -period 2.56 [get_ports clk_mac]
```

### 异步时钟域

```tcl
# 定义时钟域间的异步关系
set_clock_groups -asynchronous \
    -group {clk_dp} \
    -group {clk_cpu} \
    -group {clk_ctrl} \
    -group {clk_mac}
```

### I/O 延迟

```tcl
# 输入延迟
set_input_delay 0.2 -clock clk_dp [get_ports din*]

# 输出延迟
set_output_delay 0.2 -clock clk_dp [get_ports dout*]
```

### 时钟域交叉（CDC）

```tcl
# 从 clk_dp 到 clk_cpu 的异步信号
set_max_delay 3.0 -from [get_ports async_sig_dp] -to [get_ports async_sig_cpu]
```

### 功率约束

```tcl
# 最大功耗 200 mW
set_max_power 200 -all
```

### 多周期路径

```tcl
# 跨越多个时钟的慢路径
set_multicycle_path 2 -from [get_pins src_reg/Q] -to [get_pins dst_reg/D]
```

### 虚假路径

```tcl
# 多路选择器选择不同时钟的信号（正常不会同时发生）
set_false_path -from [get_clocks clk_dp] -to [get_clocks clk_cpu]
```

---

## 🚀 使用示例

### 运行流程模拟

```bash
cd /home/serve-ide/rv_p4/scripts/utils

# 生成演示报告
python3 simulate_flow.py

# 查看输出
less ../../implementation/design_summary.txt
```

### 生成香山黑盒

```bash
python3 gen_xs_blackbox.py

# 在 RTL 中使用黑盒
# 在 tb/cosim/cosim_main.cpp 中引用接口定义
```

### 使用约束文件

约束文件被自动使用在：
- `scripts/asic-flow/run_openroad_place.py` - 放置约束
- `scripts/asic-flow/run_openroad_route.py` - 布线约束
- `scripts/asic-flow/run_opensta.py` - 时序分析约束

**手动验证约束**：

```bash
# 使用 OpenSTA 检查约束
opensta << EOF
read_liberty \$PDK_ROOT/sky130/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__ss_100C_1v60.lib
read_verilog synthesis_results/rv_p4_netlist.v
read_sdc rv_p4.sdc

# 检查约束
check_constraint -all

# 报告时序
report_checks -digits 3

exit
EOF
```

---

## 📚 相关文档

- `../README.md` - Scripts 总览
- `../../docs/03-asic-flow/GDS_II_AUTOMATION_GUIDE.md` - 完整指南
- `../../Makefile.asic` - 自动化 Make 文件

---

## 🔗 约束文件语法参考

### 常用 SDC 命令

```tcl
# 时钟
create_clock                # 定义时钟
create_generated_clock      # 生成的时钟
set_clock_groups            # 时钟组关系
set_propagated_clock        # 传播时钟

# 延迟
set_input_delay             # 输入端延迟
set_output_delay            # 输出端延迟
set_max_delay               # 最大延迟约束
set_min_delay               # 最小延迟约束

# 时钟关系
set_multicycle_path         # 多周期路径
set_false_path              # 虚假路径

# 功率
set_max_power               # 最大功耗
set_max_leakage             # 最大泄漏功耗

# 面积
set_max_area                # 最大面积
```

---

**最后更新**：2026-02-19
