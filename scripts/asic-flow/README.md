# ASIC 设计流程脚本

完整的从 RTL 到 GDS-II 的自动化设计流程脚本。

## 📊 设计流程阶段

```
RTL 源代码
    ↓
[1. Synthesis] ← run_synthesis.sh (Yosys)
    ↓
Gate-level Netlist
    ↓
[2. Placement] ← run_openroad_place.py (OpenROAD)
    ↓
Placed DEF
    ↓
[3. Routing] ← run_openroad_route.py (OpenROAD)
    ↓
Routed DEF + SPEF
    ↓
[4. STA] ← run_opensta.py (OpenSTA)
    ↓
Timing Report
    ↓
[5. Power] ← run_power_analysis.py (Python)
    ↓
Power Report
    ↓
[6. DRC] ← run_drc.py (Magic/KLayout)
    ↓
DRC Report
    ↓
[7. LVS] ← run_lvs.py (Netgen)
    ↓
LVS Report
    ↓
[8. GDS] ← (参考 ../gds/gen_gds.py)
    ↓
GDS-II 芯片文件 ✓
```

## 🔧 各脚本详解

### 1. run_synthesis.sh

**功能**：使用 Yosys 进行逻辑综合

**用途**：
- 将 RTL (Verilog/SystemVerilog) 转换为 Gate-level 网表
- 映射到 Sky130 标准单元库
- 优化逻辑网络

**输入**：
- `rtl/**/*.sv` - RTL 源文件
- `pdk/sky130/` - Sky130 标准单元库

**输出**：
- `synthesis_results/rv_p4_netlist.v` - 综合后的网表
- `synthesis_results/rv_p4_netlist.json` - JSON 格式网表
- `synthesis_results/area_report.txt` - 面积报告
- `synthesis_results/stats_mapped.txt` - 单元统计

**用法**：
```bash
./run_synthesis.sh
```

**关键参数**：
- 目标工艺：Sky130 130nm
- 时钟目标频率：1.6 GHz
- 优化等级：-O2

### 2. run_openroad_place.py

**功能**：使用 OpenROAD 进行单元放置

**用途**：
- 将综合网表中的单元放置到芯片版图上
- 生成功率网络（power grid）
- 考虑时序和面积优化

**输入**：
- `synthesis_results/rv_p4_netlist.v` - 综合网表
- `rv_p4.sdc` - 设计约束
- `pdk/sky130/` - PDK 库

**输出**：
- `implementation/place_route/rv_p4_placed.def` - 放置后的 DEF
- `implementation/place_route/pg_net.txt` - 电源网络信息
- `implementation/place_route/logs/place.log` - 放置日志

**用法**：
```bash
python3 run_openroad_place.py
```

**主要操作**：
1. 读入网表和约束
2. 初始化放置
3. 优化放置以满足时序和功率
4. 生成电源网络
5. 输出 DEF 文件

### 3. run_openroad_route.py

**功能**：使用 OpenROAD 进行布线

**用途**：
- 为放置后的单元进行信号布线
- 生成工艺库兼容的布线
- 提取寄生参数（SPEF）

**输入**：
- `implementation/place_route/rv_p4_placed.def` - 放置的 DEF
- `rv_p4.sdc` - 约束文件
- `pdk/sky130/` - 工艺库

**输出**：
- `implementation/place_route/rv_p4_routed.def` - 布线后的 DEF
- `implementation/place_route/rv_p4.spef` - 寄生参数文件
- `implementation/place_route/logs/route.log` - 布线日志

**用法**：
```bash
python3 run_openroad_route.py
```

**主要操作**：
1. 全局布线
2. 布线优化
3. 详细布线
4. 参数提取

### 4. run_opensta.py

**功能**：使用 OpenSTA 进行静态时序分析

**用途**：
- 验证设计是否满足时序约束
- 计算 Setup/Hold slack
- 识别关键路径

**输入**：
- `implementation/place_route/rv_p4_routed.def` - 布线 DEF
- `implementation/place_route/rv_p4.spef` - 寄生参数
- `rv_p4.sdc` - 约束文件

**输出**：
- `implementation/sta/timing_report.txt` - 时序报告
- `implementation/sta/slack_summary.txt` - Slack 汇总
- `implementation/sta/critical_path.txt` - 关键路径

**用法**：
```bash
python3 run_opensta.py
```

**关键指标**：
- Setup Slack：应 ≥ 0
- Hold Slack：应 ≥ 0
- Worst Negative Slack (WNS)：应 ≥ 0

### 5. run_power_analysis.py

**功能**：进行功耗分析

**用途**：
- 估计设计的动态功耗和静态功耗
- 识别高功耗模块
- 评估功耗可行性

**输入**：
- `synthesis_results/rv_p4_netlist.v` - 网表
- 工作频率和切换活动
- 工艺库参数

**输出**：
- `implementation/power/power_report.txt` - 功耗报告
- `implementation/power/power_breakdown.json` - 功耗分解

**用法**：
```bash
python3 run_power_analysis.py
```

**输出示例**：
```
Total Power:      156 mW
  Dynamic:        150 mW (96.2%)
  Leakage:          6 mW (3.8%)
```

### 6. run_drc.py

**功能**：进行设计规则检查

**用途**：
- 验证版图是否符合工艺库设计规则
- 检查最小金属间距、线宽等
- 确保可制造性

**输入**：
- `implementation/gds/rv_p4.gds` - GDS 文件

**输出**：
- `implementation/drc/drc_report.txt` - DRC 报告
- `implementation/drc/drc_errors.gds` - 错误区域

**用法**：
```bash
python3 run_drc.py
```

**预期结果**：
```
DRC Report
===========
Total Violations: 0  ✓
Status: PASS
```

### 7. run_lvs.py

**功能**：进行 Layout vs Schematic 验证

**用途**：
- 验证 GDS 版图与网表的一致性
- 检查连接完整性
- 确保设计的逻辑正确性

**输入**：
- `implementation/gds/rv_p4.gds` - GDS 文件
- `synthesis_results/rv_p4_netlist.v` - 网表

**输出**：
- `implementation/lvs/lvs_report.txt` - LVS 报告

**用法**：
```bash
python3 run_lvs.py
```

**预期结果**：
```
LVS Report
==========
Devices:   MATCH
Nets:      MATCH
Status:    PASS ✓
```

## 🚀 完整流程运行

### 方式 1：使用 Make

```bash
cd /home/serve-ide/rv_p4
make -f Makefile.asic
```

### 方式 2：逐步手动运行

```bash
cd /home/serve-ide/rv_p4/scripts/asic-flow

# 1. 综合
./run_synthesis.sh

# 2. 放置
python3 run_openroad_place.py

# 3. 布线
python3 run_openroad_route.py

# 4. 时序分析
python3 run_opensta.py

# 5. 功耗分析
python3 run_power_analysis.py

# 6. DRC 检查
python3 run_drc.py

# 7. LVS 验证
python3 run_lvs.py
```

## 📝 约束文件参考

设计约束定义在 `rv_p4.sdc` 中：

```tcl
# 时钟定义
create_clock -name clk_dp -period 0.625 clk_dp
create_clock -name clk_cpu -period 0.667 clk_cpu
create_clock -name clk_ctrl -period 5.0 clk_ctrl
create_clock -name clk_mac -period 2.56 clk_mac

# 异步时钟域
set_clock_groups -asynchronous -group {clk_dp} -group {clk_cpu} -group {clk_ctrl} -group {clk_mac}

# I/O 延迟
set_input_delay 0.2 [get_ports din*]
set_output_delay 0.2 [get_ports dout*]
```

## ⚙️ 工具配置

### Yosys 综合选项

```tcl
# synthesis_results/yosys_synth.tcl
read_verilog -sv rtl/**/*.sv
synth_sky130 -json
```

### OpenROAD 参数

见各脚本中的 `ORD_PARAMS` 或类似配置变量。

## 🔗 依赖关系

```
run_synthesis.sh
    ↓
run_openroad_place.py
    ↓
run_openroad_route.py
    ├─► run_opensta.py
    └─► run_power_analysis.py
        ├─► run_drc.py
        └─► run_lvs.py
```

## 📚 相关文档

- `../../docs/03-asic-flow/GDS_II_AUTOMATION_GUIDE.md` - 详细指南
- `../../docs/03-asic-flow/ASIC_AREA_EVALUATION.md` - 面积评估
- `rv_p4.sdc` - 设计约束文件

---

**最后更新**：2026-02-19
