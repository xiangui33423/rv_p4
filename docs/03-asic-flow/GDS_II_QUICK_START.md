# RV-P4 GDS-II 快速开始指南

## 🚀 5 分钟快速开始

### Step 1: 安装工具链（首次，20-40 分钟）
```bash
chmod +x scripts/install_asic_tools.sh
./scripts/install_asic_tools.sh
```

### Step 2: 一键生成 GDS-II
```bash
make -f Makefile.asic all
```

### Step 3: 查看结果
```bash
ls -lh implementation/gds/rv_p4.gds
klayout implementation/gds/rv_p4.gds
```

---

## 📊 预期结果

```
┌──────────────────────────────────────┐
│ 综合          |  RTL → Netlist       │
│ 布局          |  Floorplan + Place   │
│ 布线          |  Route interconnect  │
│ 分析          |  STA + Power + DRC   │
│ 验证          |  LVS 检查            │
│ GDS 生成       |  Stream → GDS-II     │
└──────────────────────────────────────┘

输出：implementation/gds/rv_p4.gds ✓
```

---

## 📁 关键输出文件

| 文件 | 用途 | 位置 |
|------|------|------|
| **rv_p4.gds** | 最终芯片 GDS-II | `implementation/gds/` |
| **rv_p4.v** | 综合网表 | `implementation/synthesis/` |
| **timing_summary.txt** | 时序报告 | `implementation/sta/` |
| **power_summary.txt** | 功耗报告 | `implementation/power/` |
| **drc_report.txt** | DRC 检查 | `implementation/drc/` |

---

## 🛠️ 工具链

| 工具 | 用途 | 状态 |
|------|------|------|
| **Yosys** | 逻辑综合 | ✓ |
| **OpenROAD** | P&R | ✓ |
| **OpenSTA** | 时序分析 | ✓ |
| **Magic** | DRC/LVS | ✓ |
| **KLayout** | GDS 查看 | ✓ |
| **Netgen** | LVS | ✓ |
| **Sky130 PDK** | 工艺库 | ✓ |

---

## 📋 Makefile 目标速查

```bash
# 完整流程
make -f Makefile.asic all              # 所有阶段 (5-10 分钟)
make -f Makefile.asic quick-flow       # 快速流程 (4 分钟)

# 单个阶段
make -f Makefile.asic synth            # 综合
make -f Makefile.asic place            # 布局
make -f Makefile.asic route            # 布线
make -f Makefile.asic sta              # 时序分析
make -f Makefile.asic power            # 功耗分析
make -f Makefile.asic drc              # DRC
make -f Makefile.asic lvs              # LVS
make -f Makefile.asic gds              # GDS 生成

# 查看结果
make -f Makefile.asic report           # 完整报告
make -f Makefile.asic timing-report    # 时序报告
make -f Makefile.asic power-report     # 功耗报告

# 清理
make -f Makefile.asic clean            # 删除所有输出
```

---

## 📈 性能指标（预计）

```
工艺:        Sky130 130nm (开源)
芯片尺寸:    9.47 × 9.47 mm
面积:        89.8 mm²
频率:        1.6 GHz (datapath)
功耗:        ~156 mW
温度:        31°C (环境 25°C)
```

---

## 🔧 配置参数

编辑 `Makefile.asic`：

```makefile
CLOCK_PERIOD := 0.625          # 时钟周期 (ns)
TOP_MODULE := rv_p4_top        # 顶层模块
PDK_ROOT := ~/pdk              # PDK 路径
```

---

## ✅ 验证清单

- [ ] 工具安装完成
- [ ] 综合成功（rv_p4.v 生成）
- [ ] Placement 完成（*.def 生成）
- [ ] Routing 完成（*.spef 生成）
- [ ] STA 通过（WNS > 0）
- [ ] DRC 通过（0 violations）
- [ ] LVS 通过（Match）
- [ ] GDS 生成完成

---

## 🎯 下一步

1. **审查结果**
   ```bash
   cat implementation/sta/timing_summary.txt
   cat implementation/power/power_summary.txt
   ```

2. **打开 GDS**
   ```bash
   klayout implementation/gds/rv_p4.gds
   ```

3. **优化设计**
   - 编辑 SDC 约束（`scripts/rv_p4.sdc`）
   - 修改 RTL（`rtl/`）
   - 重新运行流程

4. **流片准备**
   - 收集所有报告
   - 打包设计文件
   - 向代工厂提交

---

## 📚 详细文档

- [GDS_II_AUTOMATION_GUIDE.md](./GDS_II_AUTOMATION_GUIDE.md) - 完整指南
- [Makefile.asic](./Makefile.asic) - Make 流程
- [scripts/rv_p4.sdc](./scripts/rv_p4.sdc) - 设计约束

---

## 🆘 常见问题

**Q: 工具没有安装？**
A: 运行 `./scripts/install_asic_tools.sh`

**Q: 流程卡住了？**
A: 检查日志：`cat implementation/*/logs/*.log`

**Q: 如何加速？**
A: 用 `-j` 参数：`make -j4 -f Makefile.asic all`

**Q: 能在 Windows 上运行吗？**
A: 推荐用 WSL2 或 Docker

---

**生成日期**: 2026-02-19
**完全开源** ✓
**准备就绪** ✓
