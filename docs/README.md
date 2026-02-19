# RV-P4 文档中心

本目录包含 RV-P4 项目的所有文档，按照用途分类组织。

---

## 📚 文档导航

### 1️⃣ 项目概览 [`01-project/`](./01-project/)

快速了解项目背景与规划。

| 文档 | 内容 |
|------|------|
| [**PROJECT_OVERVIEW.md**](./01-project/PROJECT_OVERVIEW.md) | 项目整体概览、架构图与设计目标 |

---

### 2️⃣ 架构设计 [`02-architecture/`](./02-architecture/)

详细的架构与设计规格文档。

| 文档 | 内容 |
|------|------|
| [**architecture.md**](./02-architecture/architecture.md) | 系统架构总体设计、时钟域、数据流、接口定义 |
| [**design_spec.md**](./02-architecture/design_spec.md) | 硬件细节规格：RTL、TCAM、SRAM、流水线参数 |
| [**COMPILER_DESIGN.md**](./02-architecture/COMPILER_DESIGN.md) | 数据平面 C-to-HW 编译器架构与设计 |
| [**P4与RISC-V交换机架构调研.md**](./02-architecture/P4与RISC-V交换机架构调研.md) | 背景研究：P4编程语言、RISC-V控制面、交换机设计原理 |

---

### 3️⃣ ASIC 工具链 [`03-asic-flow/`](./03-asic-flow/)

完整的 GDS-II 自动化设计流程文档与指南。

| 文档 | 内容 |
|------|------|
| [**GDS_II_QUICK_START.md**](./03-asic-flow/GDS_II_QUICK_START.md) | 🚀 5分钟快速开始 — 一键生成 GDS-II |
| [**GDS_II_AUTOMATION_GUIDE.md**](./03-asic-flow/GDS_II_AUTOMATION_GUIDE.md) | 📖 完整指南 — 详细说明每个设计阶段 |
| [**ASIC_AREA_EVALUATION.md**](./03-asic-flow/ASIC_AREA_EVALUATION.md) | 📊 面积评估报告 — 详细的模块面积分析 |
| [**AREA_ESTIMATION_QUICK_REFERENCE.md**](./03-asic-flow/AREA_ESTIMATION_QUICK_REFERENCE.md) | 📋 面积快速参考 — 速查表与换算公式 |

---

### 4️⃣ 可视化与报告 [`04-visualization/`](./04-visualization/)

设计的可视化展示与演示完成报告。

| 文档 | 内容 |
|------|------|
| [**VISUALIZATION_GUIDE.md**](./04-visualization/VISUALIZATION_GUIDE.md) | 🎨 可视化指南 — 如何查看 SVG 图表与 GDS 文件 |
| [**DEMO_COMPLETION_REPORT.md**](./04-visualization/DEMO_COMPLETION_REPORT.md) | ✓ 完成报告 — 全流程演示完成，所有设计阶段通过 |

> **💡 提示**：查看设计报告请打开 `visualization/index.html`（在浏览器中）

---

### 5️⃣ 参考资料 [`05-reference/`](./05-reference/)

项目结构与文件索引。

| 文档 | 内容 |
|------|------|
| [**FILES.md**](./05-reference/FILES.md) | 📁 完整文件索引 — 所有代码文件详细说明 |

---

## 🎯 快速导航

### 我想...

**📖 理解项目架构**
→ 先读 [`PROJECT_OVERVIEW.md`](./01-project/PROJECT_OVERVIEW.md)，再读 [`architecture.md`](./02-architecture/architecture.md)

**🔨 学习硬件设计细节**
→ 查看 [`design_spec.md`](./02-architecture/design_spec.md) 和 [`FILES.md`](./05-reference/FILES.md)

**⚙️ 运行 ASIC 自动化流程**
→ 先读 [`GDS_II_QUICK_START.md`](./03-asic-flow/GDS_II_QUICK_START.md)，需要详情再读 [`GDS_II_AUTOMATION_GUIDE.md`](./03-asic-flow/GDS_II_AUTOMATION_GUIDE.md)

**📊 查看设计指标**
→ 打开 `visualization/index.html` 或读 [`ASIC_AREA_EVALUATION.md`](./03-asic-flow/ASIC_AREA_EVALUATION.md)

**📈 了解面积估计方法**
→ 读 [`AREA_ESTIMATION_QUICK_REFERENCE.md`](./03-asic-flow/AREA_ESTIMATION_QUICK_REFERENCE.md)

**🎨 查看可视化图表**
→ 读 [`VISUALIZATION_GUIDE.md`](./04-visualization/VISUALIZATION_GUIDE.md)

---

## 📊 文档统计

| 类别 | 文件数 |
|------|--------|
| 项目概览 | 1 |
| 架构设计 | 3 |
| ASIC工具链 | 4 |
| 可视化报告 | 2 |
| 参考资料 | 1 |
| **总计** | **11** |

---

## 🔗 相关资源

### 根目录重要文件

- [`../README.md`](../README.md) — 项目主README（快速上手、测试运行）
- [`../visualization/index.html`](../visualization/index.html) — 交互式设计报告
- [`../implementation/gds/`](../implementation/gds/) — GDS-II 芯片文件

### 代码目录

- [`../rtl/`](../rtl/) — SystemVerilog RTL 数据平面
- [`../sw/`](../sw/) — RISC-V 控制面固件与 HAL
- [`../tb/`](../tb/) — 仿真测试台与 RTL 协同仿真
- [`../scripts/`](../scripts/) — ASIC 设计自动化脚本

---

## 🚀 快速上手流程

1. **首次使用？** → 读 [`PROJECT_OVERVIEW.md`](./01-project/PROJECT_OVERVIEW.md) (10 分钟)
2. **想运行测试？** → 回到 [`../README.md`](../README.md) 中的"快速上手"部分
3. **想生成 GDS-II？** → 读 [`GDS_II_QUICK_START.md`](./03-asic-flow/GDS_II_QUICK_START.md) (5 分钟)
4. **需要详细信息？** → 查阅对应类别的详细文档

---

**📅 文档最后更新**：2026-02-19

**📌 状态**：✓ 完整且最新
