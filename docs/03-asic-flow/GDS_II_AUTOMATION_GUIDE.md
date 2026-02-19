# RV-P4 ASIC GDS-II 自动化流程指南

完整的自动化芯片设计流程，从 RTL 到 GDS-II，所有工具均为开源。

## 📋 快速开始

### 1. 一键安装完整工具链（首次运行，20-40 分钟）

```bash
chmod +x scripts/install_asic_tools.sh
./scripts/install_asic_tools.sh
```

这将安装：
- **Yosys** - 逻辑综合
- **OpenROAD** - Place & Route
- **OpenSTA** - 时序分析
- **Magic** - DRC/LVS
- **KLayout** - GDS 查看和 DRC
- **Netgen** - LVS
- **Sky130 PDK** - 开源工艺库

### 2. 运行完整的 GDS-II 流程

```bash
# 一键生成 GDS-II（约 5-10 分钟）
make -f Makefile.asic all

# 或分步运行
make -f Makefile.asic synth      # 1. 综合
make -f Makefile.asic place      # 2. Placement
make -f Makefile.asic route      # 3. Routing
make -f Makefile.asic sta        # 4. 时序分析
make -f Makefile.asic drc        # 5. DRC 检查
make -f Makefile.asic lvs        # 6. LVS 验证
make -f Makefile.asic gds        # 7. GDS 生成
```

### 3. 查看结果

```bash
# GDS-II 文件位置
ls -lh implementation/gds/rv_p4.gds

# 打开 GDS 查看
klayout implementation/gds/rv_p4.gds

# 查看详细报告
cat implementation/sta/timing_summary.txt
cat implementation/power/power_summary.txt
cat implementation/drc/drc_report.txt
```

---

## 📁 输出目录结构

```
implementation/
├── synthesis/               # 综合结果
│   ├── rv_p4.v             # 生成的网表
│   ├── rv_p4.json          # JSON 网表
│   ├── synthesis_report.txt # 综合报告
│   └── *.log               # 详细日志
│
├── place_route/            # P&R 结果
│   ├── rv_p4_placed.def    # Placement DEF
│   ├── rv_p4_routed.def    # Routing DEF
│   ├── rv_p4_routed.spef   # 寄生参数
│   ├── placement_metrics.txt
│   └── routing_metrics.txt
│
├── sta/                    # 时序分析
│   ├── timing_summary.txt  # 时序总结
│   ├── timing_detail.txt   # 详细报告
│   └── *.rpt
│
├── power/                  # 功耗分析
│   ├── power_summary.txt   # 功耗总结
│   └── power_summary.json
│
├── drc/                    # DRC/LVS 结果
│   ├── drc_report.txt      # DRC 报告
│   ├── lvs_report.txt      # LVS 报告
│   └── *.log
│
└── gds/                    # 最终 GDS-II
    └── rv_p4.gds          # ✓ 可用于流片
```

---

## 🔧 完整工具链说明

### Makefile 目标

```bash
make -f Makefile.asic help              # 显示所有目标
make -f Makefile.asic synth             # 逻辑综合 (Yosys)
make -f Makefile.asic place             # Placement (OpenROAD)
make -f Makefile.asic route             # Routing (OpenROAD)
make -f Makefile.asic sta               # 静态时序分析 (OpenSTA)
make -f Makefile.asic power             # 功耗分析
make -f Makefile.asic drc               # DRC 检查 (Magic/KLayout)
make -f Makefile.asic lvs               # LVS 验证 (Netgen)
make -f Makefile.asic gds               # GDS-II 生成
make -f Makefile.asic report            # 生成完整总结报告
make -f Makefile.asic clean             # 删除中间文件
```

### 脚本功能说明

| 脚本 | 用途 | 工具 |
|------|------|------|
| `run_openroad_place.py` | Placement | OpenROAD |
| `run_openroad_route.py` | Routing | OpenROAD |
| `run_opensta.py` | 时序分析 | OpenSTA |
| `run_power_analysis.py` | 功耗分析 | Python |
| `run_drc.py` | DRC 检查 | Magic/KLayout |
| `run_lvs.py` | LVS 验证 | Netgen |
| `gen_gds.py` | GDS 生成 | Magic/OpenROAD |

### 约束文件

**`scripts/rv_p4.sdc`** - 设计约束文件
- 4 个独立时钟域（1.6G/1.5G/200M/390M Hz）
- I/O 延迟约束
- 多周期路径设置
- 时钟不确定度定义
- 设计规则（max fanout、max transition）

---

## 📊 预期结果估计

基于前面的面积分析和现在的功耗计算：

### 面积
```
核心逻辑:      46.5 mm²
加布线开销:    62.8 mm²
目标利用率:    70%
───────────────────────
芯片面积:      89.8 mm²
芯片尺寸:      9.47 × 9.47 mm
```

### 功耗 (130nm Sky130)
```
动态功耗:      ~150 mW
静态功耗:      ~6 mW
───────────────────────
总功耗:        ~156 mW
```

### 时序 (1.6 GHz)
```
时钟周期:      0.625 ns
目标 slack:    > 0.05 ns
```

---

## ⚙️ 配置参数

编辑 `Makefile.asic` 来修改设计参数：

```makefile
# 时钟周期（ns）- 默认 0.625 ns (1.6 GHz)
CLOCK_PERIOD := 0.625

# 其他参数
TOP_MODULE := rv_p4_top
PDK_ROOT := ~/pdk
PROCESS_NODE := 130nm
```

---

## 🚀 高级用法

### 快速流程（跳过 DRC/LVS）
```bash
make -f Makefile.asic quick-flow
# 等价于: synth -> place -> route (4 分钟)
```

### 仅综合 + 时序分析（不进行 P&R）
```bash
make -f Makefile.asic synth
make -f Makefile.asic sta
```

### 生成报告
```bash
# 完整设计总结
make -f Makefile.asic report

# 查看单个报告
make -f Makefile.asic area
make -f Makefile.asic power-report
make -f Makefile.asic timing-report
```

### 重新运行某个阶段
```bash
# 删除所有输出并重新运行
make -f Makefile.asic clean
make -f Makefile.asic all

# 仅删除 P&R 输出
rm -rf implementation/place_route
make -f Makefile.asic place
```

---

## 🔍 结果验证

### 查看综合结果
```bash
# 单元统计
grep -E "^[A-Z]" implementation/synthesis/*.txt

# 网表大小
wc -l implementation/synthesis/rv_p4.v

# 验证顶层模块
grep "^module rv_p4_top" implementation/synthesis/rv_p4.v
```

### 验证 P&R 结果
```bash
# 检查 placement
ls -lh implementation/place_route/*placed.def

# 检查 routing
ls -lh implementation/place_route/*routed.*

# 查看路由度量
cat implementation/place_route/routing_metrics.txt
```

### 验证时序
```bash
# 检查是否满足时序
grep "Slack" implementation/sta/timing_summary.txt

# 查看关键路径
head -20 implementation/sta/timing_detail.txt
```

### 验证 DRC/LVS
```bash
# DRC 检查
cat implementation/drc/drc_report.txt

# LVS 验证
cat implementation/drc/lvs_report.txt
```

### 验证 GDS
```bash
# 检查文件
ls -lh implementation/gds/rv_p4.gds

# 用 KLayout 打开
klayout implementation/gds/rv_p4.gds
```

---

## 💾 流片准备清单

完成 GDS-II 后，用于流片：

- [ ] GDS-II 文件生成完成 (`rv_p4.gds`)
- [ ] DRC 检查通过 (0 violations)
- [ ] LVS 验证通过 (Match)
- [ ] 时序签收 (WNS > 0)
- [ ] 功耗评估完成
- [ ] 物理设计完整性检查
- [ ] GDS 数据完整性验证
- [ ] 工艺库兼容性确认

### 向代工厂提交的文件

```
design_package/
├── rv_p4.gds              # 最终 GDS-II
├── rv_p4.v                # 综合后网表
├── rv_p4.spef             # 寄生参数
├── rv_p4.sdc              # 设计约束
├── README.md              # 设计说明
├── reports/
│   ├── timing_summary.txt
│   ├── power_summary.txt
│   └── area_report.txt
└── verification/
    ├── drc_report.txt
    └── lvs_report.txt
```

---

## 📚 参考资源

### 文档
- [Makefile.asic](./Makefile.asic) - 完整 Make 流程
- [rv_p4.sdc](./scripts/rv_p4.sdc) - 设计约束
- [install_asic_tools.sh](./scripts/install_asic_tools.sh) - 工具安装脚本

### 在线资源
- [Sky130 PDK Documentation](https://skywater-pdk.readthedocs.io/)
- [OpenROAD Documentation](https://openroad.readthedocs.io/)
- [Yosys Manual](http://yosyshq.net/yosys/files/YosysUserGuide.pdf)
- [OpenSTA Command Reference](https://opensta.org/)

### 工具项目
- [Yosys](https://github.com/YosysHQ/yosys)
- [OpenROAD](https://github.com/The-OpenROAD-Project/OpenROAD)
- [OpenSTA](https://github.com/The-OpenROAD-Project/OpenSTA)
- [Magic](https://github.com/RTimothyEdwards/magic)
- [Netgen](https://github.com/RTimothyEdwards/netgen)
- [KLayout](https://www.klayout.de/)

---

## ⚠️ 常见问题

### Q: 工具安装失败？
A: 运行 `sudo apt-get update && sudo apt-get upgrade -y`，然后重新运行安装脚本

### Q: Yosys 综合很慢？
A: 这是正常的。可以用 `-j` 参数加速：`make -j4 -f Makefile.asic synth`

### Q: P&R 超时？
A: 增加超时限制，编辑 `Makefile.asic` 中的 `timeout` 参数

### Q: GDS 无法生成？
A: 检查 `implementation/place_route/*_routed.def` 是否存在，确保 routing 成功

### Q: DRC/LVS 失败？
A: 这在早期可以跳过（生成中间文件）。待 P&R 稳定后再严格验证

### Q: 如何加快流程？
A: 使用 `make -f Makefile.asic quick-flow`（仅 synth + place + route）

---

## 🎯 下一步

1. **优化设计**
   - 根据功耗报告优化数据路径
   - 根据时序报告优化关键路径
   - 根据面积报告评估进一步压缩空间

2. **功耗/时序优化**
   - 添加时钟门控（CG）
   - 优化 SRAM 大小
   - 调整流水线深度

3. **流片前验证**
   - 完整的门级仿真
   - 硅前功耗评估
   - EMI/EMC 分析

4. **代工厂交互**
   - 提交 GDS 设计包
   - 进行设计审查（design review）
   - 获得代工厂反馈

---

**状态**：✓ 完整的自动化 GDS-II 流程已准备就绪

**最后更新**：2026-02-19

**生成工具**：完全开源（Yosys + OpenROAD + Sky130）
