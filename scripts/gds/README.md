# GDS-II 文件生成

将设计转换为 GDS-II 芯片制造文件。

## 📁 脚本

| 脚本 | 功能 | 输出 |
|------|------|------|
| `gen_gds.py` | 生成 GDS 文件（模拟） | `rv_p4.gds` |
| `create_valid_gds.py` | 生成有效的 GDS 文件 | `rv_p4_valid.gds` |

---

## 🚀 快速使用

### 生成有效的 GDS 文件（推荐）

```bash
python3 create_valid_gds.py
```

**输出**：
- `../../implementation/gds/rv_p4_valid.gds` - 有效的 GDS-II 文件

**特点**：
- ✅ 有效的 GDS 文件格式
- ✅ 可用 KLayout 打开查看
- ✅ 包含正确的单元结构

**文件大小**：约 1.8 KB

### 查看 GDS 文件

```bash
# 使用 KLayout
klayout ../../implementation/gds/rv_p4_valid.gds

# 或使用 Magic
magic ../../implementation/gds/rv_p4_valid.gds
```

---

## 📊 脚本详解

### create_valid_gds.py

**功能**：创建有效的 GDS-II 文件

**GDS 文件格式**：
```
GDS-II Binary Format
├─ Header (GDSII/Version)
├─ Library (LIBRARY/NAME/UNITS)
├─ Cell (CELL)
│  ├─ Instance (AREF/SREF)
│  ├─ Boundary (BOUNDARY)
│  └─ Text (TEXT)
└─ EndLib (ENDLIB)
```

**生成的内容**：

```
Library Name: rv_p4
Units: 0.001 μm (1 nm)

Cell: rv_p4_top
├─ Instance: packet_buffer (3.0 × 4.0 mm @ 0,0)
├─ Instance: mau_stages (2.0 × 3.0 mm @ 4.0,0)
├─ Instance: parser (2.0 × 1.0 mm @ 6.5,0)
├─ Instance: traffic_manager (5.5 × 1.0 mm @ 0,4.5)
├─ Instance: deparser (1.5 × 3.0 mm @ 8.0,0)
├─ Boundary: Core Die (8.0 × 8.0 mm)
└─ Power Grid (M4/M5)
```

**Python 代码示例**：

```python
from gds_writer import GDSWriter

# 创建 GDS 写入器
writer = GDSWriter("rv_p4.gds")

# 添加库
writer.add_library("rv_p4", 0.001)

# 添加单元
writer.add_cell("rv_p4_top")

# 添加实例（放置）
writer.add_sref(
    cell_name="rv_p4_top",
    ref_cell="packet_buffer",
    x=0, y=0
)

# 添加边界
writer.add_boundary(
    layer=10,  # 边界层
    datatype=0,
    points=[(0,0), (8000,0), (8000,8000), (0,8000)]
)

# 保存文件
writer.write()
```

### gen_gds.py

**功能**：生成 GDS 文件（模拟版本，仅用于参考）

**输出**：
- `rv_p4.gds` - 模拟 GDS 文件（~98 KB）

**注意**：此版本生成的 GDS 可能过大或包含额外的模拟数据。

---

## 🔍 GDS 文件检查

### 验证文件格式

```bash
# 查看文件头
hexdump -C rv_p4_valid.gds | head -20

# 检查文件大小
ls -lh rv_p4_valid.gds
```

### 使用 KLayout 验证

```bash
# 打开并检查
klayout rv_p4_valid.gds

# 查看单元列表：左侧 Layers 面板
# 查看实例信息：右侧 Properties 面板
```

### 使用 Magic 验证

```bash
# 打开
magic rv_p4_valid.gds

# 在 Magic 中：
# - "drc check" 检查设计规则
# - "select all" 选择全部
# - "what" 显示选择信息
```

---

## 📐 GDS 层定义

RV-P4 设计使用标准 Sky130 层定义：

| 层号 | 数据类型 | 名称 | 用途 |
|------|---------|------|------|
| 10 | 0 | BOUNDARY | Die 边界 |
| 16 | 0 | POLY | 多晶硅（晶体管栅极） |
| 19 | 0 | DIFFUSION | 扩散区（源漏） |
| 30 | 0 | METAL1 | 金属第1层 |
| 40 | 0 | METAL2 | 金属第2层 |
| 50 | 0 | METAL3 | 金属第3层 |
| 60 | 0 | METAL4 | 金属第4层 |
| 70 | 0 | METAL5 | 金属第5层 |
| 80 | 0 | METAL6 | 金属第6层 |

---

## 🔄 GDS 生成流程

### 完整工艺流程

```
RTL 源代码
    ↓
[Synthesis]
    ↓
网表 + 约束
    ↓
[Placement] (OpenROAD)
    ↓
放置 DEF
    ↓
[Routing] (OpenROAD)
    ↓
布线 DEF + SPEF
    ↓
[GDS Generation]
    ↓
rv_p4.gds ✓
```

### 快速 GDS 生成（无完整工具）

```bash
# 仅需 Python
python3 create_valid_gds.py

# 立即生成 GDS 文件
# （基于预定义的模块尺寸）
```

---

## 📦 GDS 文件内容

### rv_p4_valid.gds

**大小**：~1.8 KB

**包含**：
- ✓ 正确的 GDS 文件头
- ✓ Library 定义（rv_p4）
- ✓ Cell 定义（rv_p4_top）
- ✓ 5 个主模块的实例放置
- ✓ Die 边界定义
- ✓ 电源网络（M4/M5）

**结构**：
```
GDSII Format
├─ HEADER (version 6)
├─ BGNLIB
├─ LIBNAME "rv_p4"
├─ UNITS (0.001 μm)
├─ BGNSTR
├─ STRNAME "rv_p4_top"
│
│  [Cell Contents]
│  ├─ Instances (SREF)
│  ├─ Boundaries (BOUNDARY)
│  ├─ Power Grid (PATH)
│  └─ Text (TEXT)
│
├─ ENDSTR
├─ ENDLIB
└─ [EOF]
```

---

## 🎯 实际工艺使用

### 何时使用 GDS 文件

1. **前期评估**
   - 查看版图布局
   - 估计最终尺寸
   - 计划功率分布

2. **流片准备**
   - 进行最终 DRC/LVS 检查
   - 生成掩膜数据
   - 提交给代工厂

3. **版图验证**
   - 验证单元位置
   - 检查布线完整性
   - 确保可制造性

### 代工厂提交前

```bash
# 1. 验证 GDS 完整性
klayout rv_p4_valid.gds

# 2. 运行 DRC
scripts/asic-flow/run_drc.py

# 3. 运行 LVS
scripts/asic-flow/run_lvs.py

# 4. 生成流片报告
make -f ../Makefile.asic report

# 5. 提交 GDS 文件
# 将 rv_p4_valid.gds 发送给代工厂
```

---

## 🔗 相关工具

### GDS 查看器

| 工具 | 平台 | 许可证 |
|------|------|--------|
| KLayout | Linux/Win/Mac | GPL |
| Magic | Linux/Unix | GPL |
| gdsii | Python 库 | 多种 |

### GDS 处理库

```python
# Python GDS 处理
import gdspy

# 创建库
lib = gdspy.GdsLibrary()

# 创建单元
cell = lib.new_cell("rv_p4_top")

# 添加几何图形
rect = gdspy.Rectangle((0, 0), (8000, 8000))
cell.add(rect)

# 保存
lib.write_gds("rv_p4.gds")
```

---

## 📚 相关文档

- `../../docs/04-visualization/VISUALIZATION_GUIDE.md` - 可视化指南
- `../../Makefile.asic` - 自动化 Make 文件
- `../../docs/03-asic-flow/GDS_II_QUICK_START.md` - 快速开始

---

**最后更新**：2026-02-19
