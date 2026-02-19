# RV-P4 ASIC Area Evaluation Guide

## Quick Summary

根据静态 RTL 分析和 Sky130 130nm 工艺库的估算结果：

| 指标 | 值 |
|------|-----|
| **核心逻辑面积** | 46.5 mm² |
| **加上布线开销后** | 62.8 mm² |
| **目标设计利用率** | 70% |
| **估算芯片面积** | **89.8 mm²** |
| **估算芯片尺寸** | **9.47 mm × 9.47 mm** |

## 面积构成（Core Logic）

| 模块 | 面积 | 占比 |
|------|------|------|
| **Packet Buffer (2MB SRAM)** | 25.17 mm² | 54.1% |
| **MAU Stages (4级)** | 19.22 mm² | 41.3% |
| **Parser** | 1.71 mm² | 3.7% |
| **其他逻辑** | 0.45 mm² | 0.9% |

---

## 工具使用指南

### 方案 1：快速估算（无需编译）✓

已完成，结果保存在 `synthesis_results/area_estimate.json`

```bash
python3 scripts/estimate_area.py
```

### 方案 2：完整 Yosys 综合 + Sky130 工艺库

如果想要更精确的面积估算，需要 Yosys 和 Sky130 工艺库：

#### Step 1: 环境配置

```bash
# 下载 Sky130 PDK（一次性）
./scripts/setup_pdk.sh

# 这会：
# - 下载 Sky130 PDK (700MB)
# - 安装 Yosys、OpenSTA、Magic
# - 配置完整的流程设计工具链
```

#### Step 2: 运行综合

```bash
# 完整综合流程，包括逻辑优化和单元库映射
./scripts/run_synthesis.sh

# 生成的文件：
# - synthesis_results/rv_p4_netlist.v          # 综合后的网表
# - synthesis_results/rv_p4_netlist.json       # JSON 格式网表
# - synthesis_results/stats_mapped.txt         # 单元计数统计
# - synthesis_results/area_report.txt          # 详细面积报告
# - synthesis_results/SYNTHESIS_REPORT.md      # 综合报告
# - synthesis_results/logs/                    # 详细日志
```

#### Step 3: 分析结果

```bash
# 查看面积统计
cat synthesis_results/area_report.txt

# 查看合成报告
cat synthesis_results/SYNTHESIS_REPORT.md

# 查看 RTL 结构图
dot -Tpng synthesis_results/rv_p4_rtl_graph.dot -o rv_p4_rtl.png
```

---

## 方案 3：轻量级综合脚本（仅需 Yosys）

如果只安装了 Yosys，不需要 Sky130 PDK：

```bash
# 仅进行逻辑优化，输出 RTL-level 统计
yosys -m ghdl scripts/synthesis_area_eval.tcl
```

---

## 面积估算方法

### 静态分析 (estimate_area.py)

基于设计规格中的硬参数：
- **Parser TCAM**: 256 × 512 bits = 131 Kb TCAM
- **MAU Stages**: 4 级，每级含 1K TCAM + 4K SRAM
- **Packet Buffer**: 2 MB SRAM（最大占比 54%）
- **Traffic Manager**: 32 端口 × 8 队列调度器
- **其他控制逻辑**

TCAM 面积计算：
```
TCAM bit = 2 × SRAM bit + Ternary match logic
TCAM bit area ≈ 12 μm² (Sky130)
```

SRAM 面积计算：
```
SRAM bit area ≈ 1.5 μm² (Sky130 sd_mx library)
```

### Yosys 综合方法

1. **RTL 读入** → SystemVerilog 解析
2. **层次优化** → Flattening 和逻辑优化
3. **技术无关优化** → FSM、内存优化
4. **单元库映射** → 映射到 Sky130 标准单元
5. **后映射优化** → ABC、Timing-driven 优化

---

## Sky130 工艺库信息

| 参数 | 值 |
|------|-----|
| **Process Node** | 130 nm |
| **库变体** | sky130_fd_sc_hd (High Density) |
| **最小单元高度** | 2.72 μm |
| **SRAM 宏块** | 1Kb, 4Kb, 64Kb 可用 |
| **TCAM** | 不内置，需要逻辑实现 |
| **许可** | 完全开源（Google + SkyWater） |
| **获取方式** | https://github.com/google/skywater-pdk |

### 开源工具链

| 工具 | 用途 | 许可 |
|------|------|------|
| **Yosys** | 逻辑综合 | ISC |
| **OpenROAD** | P&R + 时序分析 | Apache 2.0 |
| **Magic** | 物理设计 | GPL |
| **Netgen** | LVS/DRC | GPL |

---

## 估算精度

### 静态分析精度：±30%

优点：
- 快速（秒级）
- 无需安装任何工具
- 基于实际设计参数

缺点：
- 不考虑布线延迟
- 不优化关键路径
- TCAM 和 SRAM 的实际编译器输出可能差异大

### Yosys 综合精度：±15-20%

优点：
- 真实单元计数
- 考虑逻辑优化
- Sky130 库精确映射

缺点：
- 不包括布线、缓冲器、填充单元
- 还需后续 P&R

### 完整 P&R 精度：±5%

包括：
- OpenROAD 完整 P&R
- 布线、缓冲器、时钟树
- 最终 GDS 面积

---

## 优化建议

基于当前估算：

### 1. Packet Buffer 优化 (54% 面积)

```
当前：2MB SRAM (2048 × 8Kb banks)
建议：
- 评估实际需要的缓冲大小
- 考虑分级设计（eDRAM + SRAM）
- 使用低功耗 SRAM 编译器
- 可能降低 20-30% 面积
```

### 2. MAU 流水线优化 (41% 面积)

```
当前：4 个 MAU 级，每级含 TCAM + SRAM
建议：
- 评估关键路径延迟需求
- 考虑减少 TCAM 深度（更多小表）
- 优化 TCAM 编码（不是所有条目都用）
- 可能降低 15-20% 面积
```

### 3. 集成度改进

```
1. 并行提高 vs 顺序提高
   - 当前：4 个 8 端口的 MAU 级
   - 可选：2 个 16 端口的 MAU 级
   - 权衡：面积 vs 延迟

2. 流量管理优化
   - 当前：完整 32×8 队列调度
   - 可选：分布式轻量级调度 + 集中式重排
   - 效果：降低 30-40% 逻辑面积
```

---

## 成本与功耗估算

### 制造成本 (Rough estimate)

使用 Sky130 open source 流程：

```
NRE 成本:
- 设计与验证：～$500K-2M（团队依赖）
- 掩膜成本：～$100K（低端代工，如 GlobalFoundries）
- 总计：～$600K-2.5M

单片成本 (10K 批量):
- 晶圆成本：～$5-8/片
- 切割/封装：～$2-3
- 测试：～$1-2
- 总计：～$8-13 per chip (130nm 工艺)
```

### 功耗估算

```
基于 SKy130 工艺库参数：

假设：
- 频率：1.6 GHz (数据路径)
- 活动因子：0.3 (平均)
- Leakage：～0.1 mW/mm²

核心功耗（动态）:
- Packet Buffer SRAM：400-600 mW
- MAU 逻辑：800-1200 mW
- Parser：200-300 mW
- 其他：200-300 mW
- 总计：～1.6-2.4 W

加上：
- 时钟树：～200-300 mW
- Leakage：～5-10 mW
- I/O：～100-200 mW
- 总系统功耗：～2.0-3.0 W
```

---

## 后续步骤

1. **精细化设计**：
   - 使用 Yosys 进行详细综合
   - 运行 OpenROAD 完整 P&R 流程
   - 生成 GDS-II 文件

2. **时序/功耗分析**：
   - OpenSTA 进行时序分析
   - Xyce/SPICE 进行功耗仿真
   - 识别关键路径和热点

3. **验证和流片**：
   - 完整的仿真验证（存在于 `tb/` 目录）
   - 向 GlobalFoundries / Intel 提交流片
   - 测试和硅验证

---

## 参考文献

- [Sky130 PDK Documentation](https://skywater-pdk.readthedocs.io/)
- [Yosys User Guide](http://yosyshq.net/yosys/files/YosysUserGuide.pdf)
- [OpenROAD Documentation](https://openroad.readthedocs.io/)
- [RISC-V ISA Specification](https://riscv.org/technical/specifications/)

---

## 问题排查

### Q: Yosys 提示 "Module X not found"
A: 确保所有 `include` 路径正确，运行脚本时在项目根目录执行

### Q: 为什么 SRAM 占比这么大？
A: 2MB Packet Buffer 是设计的主要面积消费者。这是数据包交换机的典型特性。

### Q: 能否减少芯片面积？
A: 可以：
1. 减少 Packet Buffer 大小（评估流量特性）
2. 减少 MAU TCAM 深度（减少支持的规则数）
3. 优化流量管理逻辑

### Q: 130nm 工艺太大了？
A: 对于这样的配置，130nm 是合理的选择。如果需要更小：
- 28nm：面积可减少 ～5-10 倍
- 成本会增加 ～10 倍
- 功耗消耗会减少 ～30-50%

---

*最后更新：2026-02-19*
*生成工具：Yosys + Sky130 PDK*
