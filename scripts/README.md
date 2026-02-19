# Scripts 目录 - RV-P4 ASIC 设计自动化脚本

本目录包含 RV-P4 ASIC 设计流程的所有自动化脚本，按照用途分类组织。

---

## 📂 目录结构

```
scripts/
├── README.md                    ← 本文件
├── asic-flow/                  ← ASIC 完整设计流程
├── setup/                       ← 环境安装与配置
├── visualization/              ← 可视化与报告生成
├── area-estimation/            ← 面积估计工具
├── gds/                        ← GDS-II 文件生成
└── utils/                      ← 其他实用工具
```

---

## 🚀 快速开始

### 方案 1：一键完整流程（推荐）

```bash
# 从项目根目录运行
make -f Makefile.asic
```

此命令会自动调用所有流程脚本，从 RTL 到最终 GDS-II。

### 方案 2：分阶段手动运行

```bash
cd scripts/asic-flow
./run_synthesis.sh
python3 run_openroad_place.py
python3 run_openroad_route.py
python3 run_opensta.py
# ... 继续其他阶段
```

---

## 📋 各子目录说明

### 1️⃣ [`asic-flow/`](./asic-flow/)

**ASIC 完整设计流程脚本**

| 脚本 | 功能 | 输入 | 输出 |
|------|------|------|------|
| `run_synthesis.sh` | Yosys 逻辑综合 | RTL (Verilog/SV) | Gate-level Netlist |
| `run_openroad_place.py` | OpenROAD 放置 | Netlist + SDC | Placed DEF |
| `run_openroad_route.py` | OpenROAD 布线 | Placed DEF | Routed DEF + SPEF |
| `run_opensta.py` | OpenSTA 时序分析 | Netlist + SPEF | Timing Report |
| `run_power_analysis.py` | 功耗分析 | Netlist | Power Report |
| `run_drc.py` | 设计规则检查 | GDS | DRC Report |
| `run_lvs.py` | Layout vs Schematic | GDS + Netlist | LVS Report |

**运行顺序**：综合 → 放置 → 布线 → 时序 → 功耗 → DRC/LVS

### 2️⃣ [`setup/`](./setup/)

**环境安装与配置脚本**

| 脚本 | 功能 |
|------|------|
| `install_asic_tools.sh` | 完整工具链安装（Yosys、OpenROAD、OpenSTA、Magic等） |
| `setup_pdk.sh` | Sky130 PDK 下载与配置 |
| `build_xiangshan.sh` | 香山 RISC-V 核编译 |

**首次运行**：
```bash
cd setup
./install_asic_tools.sh
./setup_pdk.sh
```

### 3️⃣ [`visualization/`](./visualization/)

**可视化与报告生成脚本**

| 脚本 | 功能 | 输出 |
|------|------|------|
| `generate_visualizations.py` | 生成设计流程图表（SVG 格式） | 6 个 SVG 文件 |
| `generate_svg_visualizations.py` | 替代可视化脚本 | SVG 设计图 |

**用途**：
- 生成架构图、面积分布、时序闭合等可视化图表
- 输出到 `visualization/` 目录供展示或文档使用

### 4️⃣ [`area-estimation/`](./area-estimation/)

**面积估计与评估工具**

| 脚本 | 功能 | 精度 |
|------|------|------|
| `estimate_area.py` | Python 静态 RTL 分析 | ±30% |
| `synthesis_area_eval.tcl` | Yosys TCL 脚本 | ±15-20% |

**用途**：
- 快速评估 ASIC 面积
- 模块级别面积分解
- 识别面积瓶颈

**示例**：
```bash
cd area-estimation
python3 estimate_area.py
# 输出：synthesis_results/area_estimate.json
```

### 5️⃣ [`gds/`](./gds/)

**GDS-II 文件生成脚本**

| 脚本 | 功能 |
|------|------|
| `gen_gds.py` | GDS-II 生成（模拟版本） |
| `create_valid_gds.py` | 生成有效的 GDS-II 文件 |

**输出**：
- `implementation/gds/rv_p4.gds` - 模拟 GDS 文件
- `implementation/gds/rv_p4_valid.gds` - 有效 GDS 文件（可用 KLayout 打开）

### 6️⃣ [`utils/`](./utils/)

**其他实用工具与配置**

| 文件 | 用途 |
|------|------|
| `simulate_flow.py` | 完整流程模拟（无需实际工具） |
| `gen_xs_blackbox.py` | 香山黑盒生成器 |
| `rv_p4.sdc` | 设计约束文件（SDC 格式） |

**特别说明**：
- `simulate_flow.py`：当实际工具不可用时，用来演示整个设计流程
- `rv_p4.sdc`：定义时序约束、时钟域、I/O 延迟等

---

## 🔧 文件依赖关系

```
Makefile.asic (根目录)
  ├─► asic-flow/run_synthesis.sh
  ├─► asic-flow/run_openroad_place.py
  ├─► asic-flow/run_openroad_route.py
  ├─► asic-flow/run_opensta.py
  ├─► asic-flow/run_power_analysis.py
  ├─► asic-flow/run_drc.py
  ├─► asic-flow/run_lvs.py
  ├─► gds/gen_gds.py
  ├─► visualization/generate_visualizations.py
  └─► area-estimation/estimate_area.py

setup/ (初始化)
  ├─► setup/install_asic_tools.sh
  ├─► setup/setup_pdk.sh
  └─► setup/build_xiangshan.sh
```

---

## 💾 输入输出位置

### 输入文件

| 来源 | 路径 |
|------|------|
| RTL 源代码 | `rtl/**/*.sv` |
| 设计约束 | `scripts/utils/rv_p4.sdc` |
| 工艺库 | `pdk/sky130/...`（由 setup 脚本安装） |

### 输出文件

| 阶段 | 输出路径 |
|------|---------|
| 综合 | `synthesis_results/` |
| 放置 | `implementation/place_route/` |
| 布线 | `implementation/place_route/` |
| 时序 | `implementation/sta/` |
| 功耗 | `implementation/power/` |
| DRC/LVS | `implementation/drc/`, `implementation/lvs/` |
| GDS | `implementation/gds/` |
| 可视化 | `visualization/` |

---

## 🛠️ 常见用途

### 只做面积估计

```bash
cd area-estimation
python3 estimate_area.py
# 查看：synthesis_results/area_estimate.json
```

### 只做时序分析

```bash
cd asic-flow
./run_synthesis.sh
python3 run_opensta.py
# 查看：implementation/sta/timing_report.txt
```

### 生成可视化报告

```bash
cd visualization
python3 generate_visualizations.py
# 打开：visualization/*.svg
```

### 完整流程模拟（无需工具）

```bash
cd utils
python3 simulate_flow.py
# 输出完整设计流程演示
```

---

## 📖 脚本使用指南

### 前置要求

- Python 3.7+
- Bash 4.0+（for shell scripts）

### 可选工具（取决于脚本）

| 工具 | 脚本 | 安装 |
|------|------|------|
| Yosys | asic-flow/ | `setup/install_asic_tools.sh` |
| OpenROAD | asic-flow/ | `setup/install_asic_tools.sh` |
| OpenSTA | asic-flow/ | `setup/install_asic_tools.sh` |
| Sky130 PDK | asic-flow/ | `setup/setup_pdk.sh` |
| KLayout | visualization/ | `setup/install_asic_tools.sh` |

### 环境变量

部分脚本需要设置环境变量：

```bash
# ASIC 工具链
export PDK=/path/to/sky130
export YOSYS=/usr/local/bin/yosys
export OPENROAD=/usr/local/bin/openroad
export OPENSTA=/usr/local/bin/opensta

# 或运行 setup 脚本自动配置
source setup/install_asic_tools.sh
```

---

## 🐛 调试与日志

大多数脚本生成详细日志：

```bash
# 查看综合日志
tail -f synthesis_results/logs/synthesis.log

# 查看放置日志
tail -f implementation/place_route/logs/place.log

# 查看布线日志
tail -f implementation/place_route/logs/route.log
```

---

## 📝 脚本开发指南

### 添加新脚本

1. 按照用途放入相应子目录
2. 添加完整的注释和帮助信息
3. 在对应 `README.md` 中添加说明
4. 更新 `Makefile.asic` 的依赖关系（如需要）

### 脚本模板

```bash
#!/bin/bash
# 脚本功能简述

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'  # No Color

# 日志函数
log_info() { echo -e "${GREEN}[INFO]${NC} $@"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $@"; }
log_error() { echo -e "${RED}[ERROR]${NC} $@"; exit 1; }

# 脚本内容...
```

---

## 🔗 相关文档

- [`../docs/03-asic-flow/GDS_II_AUTOMATION_GUIDE.md`](../docs/03-asic-flow/GDS_II_AUTOMATION_GUIDE.md) - 完整自动化指南
- [`../docs/03-asic-flow/GDS_II_QUICK_START.md`](../docs/03-asic-flow/GDS_II_QUICK_START.md) - 快速开始
- [`../Makefile.asic`](../Makefile.asic) - 自动化 Make 文件
- [`../docs/README.md`](../docs/README.md) - 文档导航中心

---

## 📞 故障排除

### 脚本权限问题

```bash
# 添加执行权限
chmod +x asic-flow/*.sh
chmod +x setup/*.sh
chmod +x visualization/*.py
```

### 工具找不到

```bash
# 检查工具路径
which yosys
which openroad
which opensta

# 如未找到，运行安装脚本
cd setup
./install_asic_tools.sh
```

### Python 模块缺失

```bash
# 安装必需的 Python 包
pip install pyyaml jinja2 click
```

---

**📅 最后更新**：2026-02-19

**📌 状态**：✓ 完整与最新
