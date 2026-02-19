# 面积估计工具

快速评估 ASIC 芯片面积。

## 📊 工具

| 工具 | 精度 | 速度 | 依赖 |
|------|------|------|------|
| `estimate_area.py` | ±30% | <1s | Python 3 |
| `synthesis_area_eval.tcl` | ±15-20% | 1-5m | Yosys |

---

## 🚀 快速使用

### Python 静态分析（推荐首选）

```bash
python3 estimate_area.py
```

**输入**：
- `../../rtl/**/*.sv` - RTL 源代码

**输出**：
- `../../synthesis_results/area_estimate.json` - 面积估计结果
- 屏幕输出：汇总表格

**示例输出**：
```
╔════════════════════════════════════════════╗
║         RV-P4 Area Estimation              ║
╚════════════════════════════════════════════╝

核心面积分解:
  Packet Buffer .............. 25.20 mm² (54.1%)
  MAU Stages (4×) ........... 19.20 mm² (41.3%)
  Parser ..................... 1.70 mm² (3.7%)
  其他 ....................... 0.50 mm² (0.9%)
  ─────────────────────────────────────────
  核心面积 .................. 46.50 mm²

最终面积 (含布线和利用率):
  ├─ 布线开销 (×1.35) ....... 62.78 mm²
  └─ 利用率 (70%) ............ 89.75 mm²
```

### Yosys 详细评估

```bash
# 需要安装 Yosys
yosys -c synthesis_area_eval.tcl
```

**优势**：
- 更精确的面积估计
- 单元类型详细分布
- 真实的综合后结果

**输出**：
- `synthesis_results/area_report.txt` - 详细报告
- `synthesis_results/stats_mapped.txt` - 单元统计

---

## 📁 Python 脚本详解

### estimate_area.py

**功能**：静态 RTL 分析

**算法**：
1. 解析 RTL 代码，识别内存和逻辑
2. 根据 Sky130 库参数估计面积
3. 按模块汇总结果

**主要参数**（可在脚本中修改）：

```python
# SRAM 面积系数（Sky130）
SRAM_AREA_PER_BIT = 1.5e-6  # mm²/bit

# TCAM 面积系数
TCAM_AREA_PER_BIT = 12e-6   # mm²/bit

# 逻辑面积系数
LOGIC_AREA_PER_GATE = 0.01  # mm² per 2-input gate
```

**假设**：
- SRAM：1.5 μm²/bit（Sky130）
- TCAM：12 μm²/bit（约为 SRAM 的 8 倍）
- 逻辑：0.01 mm²/gate

**精度级别**：
- 静态分析：±30%（快速但不精确）
- 需要更精确结果 → 使用 Yosys 综合

### synthesis_area_eval.tcl

**功能**：Yosys TCL 脚本用于详细面积评估

**流程**：
1. 读入 RTL
2. 综合到 Sky130 库
3. 统计单元面积

**使用**：
```bash
yosys synthesis_area_eval.tcl
```

---

## 📊 面积构成详解

### 1. Packet Buffer (54% - 25.2 mm²)

```
2 MB 缓冲区
= 2048 KB × 8 bits/byte
= 16.7 Mb SRAM

面积 = 16.7M bits × 1.5 μm²/bit ≈ 25.2 mm²
```

**减小方法**：
- 评估实际流量 → 可能减少缓冲大小
- 分层设计（eDRAM + SRAM）
- 压缩存储格式

### 2. MAU Stages (41% - 19.2 mm²)

```
4 级 MAU，每级：
  TCAM: 1024 entries × 320 bits = 327 Kb
  SRAM: 4096 entries × 128 bits = 512 Kb
  逻辑: ALU, Hash, Mux

总面积 ≈ 19.2 mm²
```

**减小方法**：
- 减少 TCAM 深度
- 减少 SRAM 宽度
- 优化流水线深度

### 3. Parser (4% - 1.7 mm²)

```
P4 包解析引擎
- 相对固定，难以优化
- 面积占比最小
```

### 4. 其他 (1%)

```
Traffic Manager, Deparser, TUE, 控制逻辑等
```

---

## 🔄 工作流程

```
RTL 源代码
    ↓
estimate_area.py ← (快速，±30%)
    ↓
area_estimate.json
    ↓
(需要更精确?)
    ↓
run_synthesis.sh ← (详细，±15-20%)
    ↓
synthesis_results/
```

---

## 🔗 输出文件

### area_estimate.json

JSON 格式的面积估计结果：

```json
{
  "process": "Sky130 130nm",
  "modules": {
    "packet_buffer": {
      "type": "SRAM",
      "area_mm2": 25.2,
      "percentage": 54.1
    },
    ...
  },
  "summary": {
    "total_core_area_mm2": 46.5,
    "estimated_die_area_mm2": 89.8,
    "utilization": "70%"
  }
}
```

**用途**：
- 编程接口读取（Python/JSON）
- 与其他工具集成
- 版本控制和追踪

### area_report.txt

人类可读的面积报告：

```
RV-P4 Area Estimation Report
============================

模块名称          类型    面积(mm²)  占比
────────────────────────────────────────
Packet Buffer    SRAM      25.20    54.1%
MAU Stages(0)    Mixed     4.80     10.3%
MAU Stages(1)    Mixed     4.80     10.3%
MAU Stages(2)    Mixed     4.80     10.3%
MAU Stages(3)    Mixed     4.80     10.3%
Parser           Logic     1.70      3.7%
Traffic Manager  Logic     0.30      0.7%
...
────────────────────────────────────────
总面积                    46.50   100.0%

最终芯片面积（含开销）:
  布线开销 (35%) .......... 62.78 mm²
  利用率 (70%) ............ 89.75 mm²
```

---

## 💾 集成到其他工具

### Python 脚本中读取

```python
import json

with open('synthesis_results/area_estimate.json') as f:
    data = json.load(f)

core_area = data['summary']['total_core_area_mm2']
die_area = data['summary']['estimated_die_area_mm2']

print(f"Core: {core_area:.2f} mm²")
print(f"Die:  {die_area:.2f} mm²")
```

### Bash 脚本中使用

```bash
# 提取核心面积
CORE_AREA=$(python3 -c "import json; d=json.load(open('synthesis_results/area_estimate.json')); print(d['summary']['total_core_area_mm2'])")

echo "Core area: $CORE_AREA mm²"
```

---

## ⚙️ 参数调整

### 修改面积系数

编辑 `estimate_area.py`：

```python
# 针对不同工艺调整
if PROCESS == "Sky130":
    SRAM_AREA_PER_BIT = 1.5e-6
elif PROCESS == "28nm":
    SRAM_AREA_PER_BIT = 0.7e-6
```

### 布线开销调整

```python
# 增加布线开销（密集型设计）
ROUTING_OVERHEAD = 1.5  # 1.35 → 1.5

# 减少布线开销（稀疏设计）
ROUTING_OVERHEAD = 1.2
```

---

## 📚 相关文档

- `../../docs/03-asic-flow/ASIC_AREA_EVALUATION.md` - 完整评估指南
- `../../docs/03-asic-flow/AREA_ESTIMATION_QUICK_REFERENCE.md` - 快速参考
- `../../synthesis_results/README.md` - 综合结果说明

---

**最后更新**：2026-02-19
