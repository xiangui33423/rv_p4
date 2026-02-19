# RV-P4 C-to-HW 编译器设计文档

## 目录

1. [概述](#1-概述)
2. [编译器架构](#2-编译器架构)
3. [前端：词法与语法分析](#3-前端词法与语法分析)
4. [中间层：语义分析与优化](#4-中间层语义分析与优化)
5. [后端：二进制代码生成](#5-后端二进制代码生成)
6. [内置动作库](#6-内置动作库)
7. [输出文件格式](#7-输出文件格式)
8. [C 语言标注参考](#8-c-语言标注参考)
9. [C 子集约束](#9-c-子集约束)
10. [用法与示例](#10-用法与示例)
11. [已知局限](#11-已知局限)

---

## 1. 概述

`rvp4cc.py` 是 RV-P4 项目的数据面编译器，将带有特殊 `__attribute__` 标注的 C 源文件编译为可直接烧录到交换芯片的硬件配置包（`.hwcfg`）。

### 1.1 在整体系统中的位置

```
dataplane.c（用户 C 代码）
        │
        ▼
   rvp4cc.py（本编译器）
        │
        ├─ parser_tcam.bin          → Parser TCAM 初始化镜像
        ├─ mau_tcam_init_sN.bin     → 各 Stage TCAM 镜像（×24）
        ├─ mau_asram_init_sN.bin    → 各 Stage Action SRAM 镜像（×24）
        ├─ phv_map.json             → PHV 字段映射表
        ├─ table_info.json          → 表元信息
        ├─ action_info.json         → 动作元信息
        └─ dataplane.hwcfg          → 上述文件的 tar.gz 打包
```

数据面 C 代码在**编译时**完全转化为硬件配置，运行时不消耗 RISC-V CPU 资源。控制面 C 代码（`sw/firmware/`）则通过标准 RISC-V GCC 工具链编译为 ELF，运行时通过 HAL 动态操作表项。

### 1.2 硬件常量（与 RTL 保持一致）

| 常量 | 值 | 对应 RTL 参数 |
|------|-----|--------------|
| `NUM_MAU_STAGES` | 24 | `rv_p4_pkg.sv: NUM_MAU_STAGES` |
| `PARSER_TCAM_DEPTH` | 256 | `p4_parser.sv` |
| `PARSER_TCAM_WIDTH` | 640 bit | `parser_tcam.sv` |
| `MAU_TCAM_DEPTH` | 2048 | `mau_tcam.sv` |
| `MAU_TCAM_KEY_W` | 512 bit | `mau_tcam.sv` |
| `MAU_ASRAM_DEPTH` | 65536 | `mau_stage.sv` |
| `MAU_ASRAM_WIDTH` | 128 bit | `mau_stage.sv` |
| `PHV_BYTES` | 512 | `rv_p4_pkg.sv: PHV_W` |

---

## 2. 编译器架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    RV-P4 C 编译器（rvp4cc.py）                   │
│                                                                  │
│  输入: dataplane.c                                               │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  前端（Frontend）                          rvp4cc.py      │   │
│  │  ├── strip_comments()   注释剥离            L131          │   │
│  │  ├── parse_attributes() __attribute__ 解析  L137          │   │
│  │  └── parse_functions()  函数定义提取         L146          │   │
│  └─────────────────────────┬────────────────────────────────┘   │
│                             │ List[dict]（函数列表）              │
│  ┌──────────────────────────▼────────────────────────────────┐  │
│  │  中间层（Middle-end）              RVP4Compiler 类          │  │
│  │  ├── _register_table()   构建 TableDef         L281        │  │
│  │  ├── _register_action()  构建 ActionDef        L301        │  │
│  │  ├── _register_parser_state() 构建 ParserState L308        │  │
│  │  ├── _assign_stages()    Stage 贪心分配        L320        │  │
│  │  └── _validate()         约束检查              L332        │  │
│  └─────────────────────────┬────────────────────────────────┘  │
│                             │ 内部 IR（tables / actions / states）│
│  ┌──────────────────────────▼────────────────────────────────┐  │
│  │  后端（Backend）                  二进制/JSON 生成           │  │
│  │  ├── gen_parser_tcam_bin()   → parser_tcam.bin   L346      │  │
│  │  ├── gen_mau_tcam_bin()      → mau_tcam_init.bin L398      │  │
│  │  ├── gen_mau_asram_bin()     → mau_asram_init.bin L408     │  │
│  │  ├── gen_phv_map()           → phv_map.json      L428      │  │
│  │  ├── gen_table_info()        → table_info.json   L432      │  │
│  │  ├── gen_action_info()       → action_info.json  L445      │  │
│  │  ├── gen_report()            → dataplane.report  L460      │  │
│  │  └── package()               → dataplane.hwcfg  L488      │  │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  输出: dataplane.hwcfg                                           │
└──────────────────────────────────────────────────────────────────┘
```

主编译入口（`compile()` 方法，`L262`）的调用顺序：

```python
cc = RVP4Compiler()
cc.compile(src)      # 前端 + 中间层
cc.package(...)      # 后端：打包所有输出
```

---

## 3. 前端：词法与语法分析

### 3.1 注释剥离（`strip_comments`，L131）

使用两条正则表达式去除 C 注释：

```python
src = re.sub(r'/\*.*?\*/', '', src, flags=re.DOTALL)  # 块注释
src = re.sub(r'//[^\n]*',  '', src)                   # 行注释
```

### 3.2 `__attribute__` 解析（`parse_attributes`，L137）

识别 GCC 兼容的 `__attribute__((name(args...)))` 语法，返回 `Attribute` 数据类列表。

正则模式匹配：

```
__attribute__\s*\(\s*\(\s*(\w+)(?:\s*\(([^)]*)\))?\s*\)\s*\)
                            ^^^^         ^^^^^^^^
                            名称         可选参数列表（逗号分隔）
```

示例：

| 源代码 | 解析结果 |
|--------|---------|
| `__attribute__((rvp4_table))` | `Attribute(name='rvp4_table', args=[])` |
| `__attribute__((rvp4_stage(0)))` | `Attribute(name='rvp4_stage', args=['0'])` |
| `__attribute__((rvp4_size(2048)))` | `Attribute(name='rvp4_size', args=['2048'])` |

### 3.3 函数定义提取（`parse_functions`，L146）

使用多行正则模式，识别带有一个或多个 `__attribute__` 块的函数定义：

```
(一个或多个 __attribute__ 块) + (返回类型可选) + (函数名) + (参数列表)
```

对每个匹配，回调 `_process_function()` 分发到对应注册器：

```
attrs 包含 rvp4_table   →  _register_table()
attrs 包含 rvp4_action  →  _register_action()
attrs 包含 rvp4_parser  →  _register_parser_state()
```

---

## 4. 中间层：语义分析与优化

### 4.1 内部数据结构（IR）

编译器使用三种 Python dataclass 作为内部表示（IR）：

#### `TableDef`（L104）

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 表名（C 函数名） |
| `stage` | `int` | 分配的 MAU Stage（-1 = 待分配） |
| `table_id` | `int` | 自增表 ID |
| `match_type` | `str` | `"lpm"` / `"exact"` / `"ternary"` |
| `size` | `int` | 最大条目数 |
| `key_fields` | `List[str]` | 匹配键字段名（预留） |
| `actions` | `List[str]` | 关联动作名（预留） |

#### `ActionDef`（L96）

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 动作名 |
| `action_id` | `int` | 16 位动作 ID |
| `primitives` | `List[ActionPrimitive]` | 原语列表 |
| `params` | `List[str]` | 参数名列表 |

#### `ActionPrimitive`（L89）

| 字段 | 类型 | 说明 |
|------|------|------|
| `op` | `int` | ALU 操作码（见 §6） |
| `dst_off` | `int` | 目标 PHV 字节偏移 |
| `src_off` | `int` | 源 PHV 字节偏移 |
| `imm_val` | `int` | 立即数（32 bit） |
| `fwidth` | `int` | 操作字段宽度（字节） |

#### `ParserState`（L113）

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 状态名 |
| `state_id` | `int` | 自增状态 ID |
| `key_offset` | `int` | 匹配键在报文中的字节偏移 |
| `key_len` | `int` | 匹配键长度（字节） |
| `key_val / key_mask` | `int` | TCAM key/mask 值 |
| `next_state` | `int` | 跳转目标状态 ID |
| `extract_offset/len` | `int` | 提取字段在报文中的位置与长度 |
| `phv_dst_offset` | `int` | 写入 PHV 的目标偏移 |
| `hdr_advance` | `int` | 报文头指针前进量（字节） |

### 4.2 表注册（`_register_table`，L281）

1. 从 `rvp4_stage(N)` 属性读取 Stage，不存在则置 -1（待分配）。
2. 从 `rvp4_size(N)` 读取大小，默认 256。
3. 通过 `rvp4_lpm` / `rvp4_exact` / `rvp4_ternary` 确定匹配类型，默认 `ternary`。
4. 分配自增 `table_id` 并存入 `self.tables` 字典。

### 4.3 Stage 自动分配（`_assign_stages`，L320）

对所有 `stage == -1` 的表，采用贪心策略依次分配最小可用 Stage：

```python
used = {t.stage for t in self.tables.values() if t.stage >= 0}
next_free = 0
for t in self.tables.values():
    if t.stage < 0:
        while next_free in used:
            next_free += 1
        t.stage = next_free
        used.add(next_free)
```

用户可通过 `rvp4_stage(N)` 手动指定，不指定则按顺序自动填充空闲 Stage。

### 4.4 约束验证（`_validate`，L332）

| 检查项 | 错误/警告 | 阈值 |
|--------|---------|------|
| Stage 越界 | **错误**（终止编译） | `>= NUM_MAU_STAGES (24)` |
| 表大小超出 TCAM 深度 | **警告**（截断） | `> MAU_TCAM_DEPTH (2048)` |

---

## 5. 后端：二进制代码生成

### 5.1 Parser TCAM 镜像（`gen_parser_tcam_bin`，L346）

**格式**：`256 条目 × 80 字节（640 bit）`

每条 TCAM 条目的编码格式（简化版）：

```
偏移  字段           大小
 0    key_val         8 B   (uint64, little-endian)
 8    key_mask        8 B   (uint64, little-endian)
16    next_state      1 B
17    extract_offset  1 B   (报文字节偏移)
18    extract_len     1 B   (提取字节数)
19    phv_dst_hi      1 B   (PHV目标偏移高字节)
20    phv_dst_lo      1 B   (PHV目标偏移低字节)
21    hdr_advance     1 B   (头指针前进量)
22    valid           1 B   (1=有效)
23-79 padding         57 B
```

**预置的 3 条内置状态：**

| 索引 | 含义 | key | mask | next_state |
|------|------|-----|------|-----------|
| 0 | START → parse_ethernet | 0 | 0（通配） | 1 |
| 1 | EtherType=0x0800 → parse_ipv4 | 0x0800 | 0xFFFF | 2 |
| 2 | parse_ipv4 → ACCEPT | 0 | 0 | 0 |

用户 `rvp4_parser` 函数产生的状态从索引 3 开始追加。

### 5.2 MAU TCAM 镜像（`gen_mau_tcam_bin`，L398）

**格式**：`2048 条目 × 128 字节`

```
偏移  字段           大小
 0    key             64 B  (512 bit 匹配键)
64    mask            64 B  (512 bit 掩码)
128   action_id        2 B  (指向 Action SRAM 的槽位)
130   action_ptr       2 B  (Action SRAM 直接指针)
132   valid            1 B
133   pad             55 B
```

编译时所有条目初始化为全 0（无效）。控制面固件在运行时通过 TUE（Table Update Engine）动态写入条目。

### 5.3 Action SRAM 镜像（`gen_mau_asram_bin`，L408）

**格式**：`65536 条目 × 16 字节（128 bit）`

```
偏移  字段           大小
 0    action_id        2 B  (uint16)
 2    action_params   14 B  (112 bit 参数编码)
```

**`action_params` 编码（`encode_action_params`，L176）：**

| 位域 | 字段 | 说明 |
|------|------|------|
| [111:102] | `dst_offset` (10 bit) | 目标 PHV 字节偏移 |
| [79:70] | `src_offset` (10 bit) | 源 PHV 字节偏移 |
| [47:16] | `imm_value` (32 bit) | 立即数 |
| [15:8] | `field_width` (8 bit) | 操作宽度（字节） |

槽位分配规则：`slot = action_id & 0xFFFF`（直接索引）。

**`action_id` 编码（`make_action_id`，L196）：**

```
[15:12] op (4 bit)   — ALU 操作码
[11:8]  group (4 bit)— 字段组
[7:0]   sub_op (8bit)— 子操作
```

### 5.4 PHV 字段映射（`gen_phv_map`，L428）

输出 JSON，记录每个 PHV 字段的字节偏移和宽度：

```json
{
  "eth_dst":      {"offset":  0, "width": 6},
  "eth_src":      {"offset":  6, "width": 6},
  "eth_type":     {"offset": 12, "width": 2},
  "vlan_tci":     {"offset": 14, "width": 2},
  "ipv4_dscp":    {"offset": 19, "width": 1},
  "ipv4_src":     {"offset": 30, "width": 4},
  "ipv4_dst":     {"offset": 34, "width": 4},
  ...
}
```

### 5.5 打包（`package`，L488）

将所有生成文件写入 `tar.gz` 压缩包：

```python
files["parser_tcam.bin"]          # 1 个文件
files["mau_tcam_init_sN.bin"]     # 24 个文件（N=0..23）
files["mau_asram_init_sN.bin"]    # 24 个文件（N=0..23）
files["phv_map.json"]
files["table_info.json"]
files["action_info.json"]
files["dataplane.report"]
```

---

## 6. 内置动作库

编译器内置 9 个预定义动作，直接写入 Action SRAM：

| 动作名 | action_id | ALU op | 说明 |
|--------|-----------|--------|------|
| `nop` | 0x0000 | `OP_NOP` | 空操作 |
| `forward` | 0x1001 | `OP_SET_PORT` | 设置出端口（param: port） |
| `drop` | 0x1002 | `OP_DROP` | 丢弃报文 |
| `permit` | 0x2001 | `OP_NOP` | ACL 放行 |
| `deny` | 0x2002 | `OP_DROP` | ACL 拒绝 |
| `l2_forward` | 0x3001 | `OP_SET_PORT` | L2 转发（param: port） |
| `flood` | 0x3002 | `OP_NOP` | 泛洪（标志设置） |
| `set_ttl_dec` | 0x4001 | `OP_ADD` | TTL 递减（imm=-1） |
| `set_dscp` | 0x4002 | `OP_SET` | 设置 DSCP（param: dscp） |

用户自定义动作从 ID `0x5000` 开始自增分配。

### ALU 操作码

| 操作码 | 名称 | 功能 | 对应 `mau_alu.sv` |
|--------|------|------|-------------------|
| 0x0 | `OP_NOP` | 无操作 | `NOP` |
| 0x1 | `OP_SET` | `dst ← imm` | `SET` |
| 0x2 | `OP_COPY` | `dst ← src` | `COPY` |
| 0x3 | `OP_ADD` | `dst ← dst + imm` | `ADD` |
| 0x4 | `OP_SUB` | `dst ← dst - imm` | `SUB` |
| 0x5 | `OP_AND` | `dst ← dst & imm` | `AND` |
| 0x6 | `OP_OR` | `dst ← dst \| imm` | `OR` |
| 0x7 | `OP_XOR` | `dst ← dst ^ imm` | `XOR` |
| 0x8 | `OP_SET_META` | 设置元数据 | `SET_META` |
| 0x9 | `OP_DROP` | 设置丢弃标志 | `DROP` |
| 0xA | `OP_SET_PORT` | 设置出端口 | `SET_PORT` |
| 0xB | `OP_SET_PRIO` | 设置 QoS 优先级 | `SET_PRIO` |
| 0xC | `OP_HASH_SET` | 设置哈希结果 | `HASH_SET` |
| 0xD | `OP_COND_SET` | 条件赋值 | `COND_SET` |

---

## 7. 输出文件格式

| 文件名 | 格式 | 大小 | 用途 |
|--------|------|------|------|
| `parser_tcam.bin` | 二进制 | 20 480 B (256×80) | 烧录 Parser TCAM |
| `mau_tcam_init_sN.bin` | 二进制 | 262 144 B (2048×128) | Stage N TCAM 初始镜像 |
| `mau_asram_init_sN.bin` | 二进制 | 1 048 576 B (65536×16) | Stage N Action SRAM 初始镜像 |
| `phv_map.json` | JSON | ~500 B | 字段名→PHV 偏移 |
| `table_info.json` | JSON | 动态 | 表→Stage 映射 |
| `action_info.json` | JSON | 动态 | 动作 ID 及参数定义 |
| `dataplane.report` | 文本 | ~1 KB | 资源使用摘要 |
| `dataplane.hwcfg` | tar.gz | ~50 MB | 全部文件打包 |

---

## 8. C 语言标注参考

编译器通过 GCC 兼容的 `__attribute__` 扩展识别数据面语义：

| 标注 | 作用域 | 说明 |
|------|--------|------|
| `__attribute__((rvp4_parser))` | 函数 | 标记为 Parser 状态处理函数 |
| `__attribute__((rvp4_table))` | 函数 | 标记为 MAU 表查找函数 |
| `__attribute__((rvp4_action))` | 函数 | 标记为动作函数 |
| `__attribute__((rvp4_phv_field))` | 结构体成员 | 标记为 PHV 字段 |
| `__attribute__((rvp4_metadata))` | 结构体成员 | 标记为元数据字段 |
| `__attribute__((rvp4_stage(N)))` | 函数 | 指定分配到 Stage N（0-23） |
| `__attribute__((rvp4_priority(N)))` | 表条目 | 指定 TCAM 优先级 |
| `__attribute__((rvp4_exact))` | 表 | 精确匹配 |
| `__attribute__((rvp4_lpm))` | 表 | 最长前缀匹配 |
| `__attribute__((rvp4_ternary))` | 表 | 三值匹配（默认） |
| `__attribute__((rvp4_size(N)))` | 表 | 指定表最大条目数 |

---

## 9. C 子集约束

数据面 C 代码受以下约束限制，编译器在 `_validate()` 中强制检查：

| 约束类别 | 允许 | 禁止 |
|----------|------|------|
| 数据类型 | `uint8/16/32/64_t`, `bool` | `float`, `double`, 任意指针（除 PHV 引用） |
| 控制流 | `if/else`, `switch` | `for/while/do-while`（Parser 循环除外） |
| 函数调用 | 标注函数间调用 | 递归, 函数指针 |
| 内存访问 | PHV 字段, Stateful SRAM | 动态分配, 全局变量写 |
| 算术运算 | `+`, `-`, `&`, `\|`, `^`, `~`, `<<`, `>>` | `/`, `%`（硬件无除法器）|
| 表大小 | 最大 2048 条目/Stage | 超过 2048（TCAM 物理限制） |
| Stage 编号 | 0–23 | ≥ 24 |

---

## 10. 用法与示例

### 10.1 编译命令

```bash
# 基本编译
python3 rvp4cc.py example_dataplane.c -o dataplane.hwcfg

# 同时输出编译报告（stdout）
python3 rvp4cc.py example_dataplane.c -o dataplane.hwcfg --report

# 同时将 JSON 元信息写到输出目录
python3 rvp4cc.py example_dataplane.c -o dataplane.hwcfg --dump-json
```

### 10.2 最小 C 示例

```c
#include <stdint.h>

// 声明 Parser 状态
__attribute__((rvp4_parser))
void parse_ethernet(void) { }

// 声明动作
__attribute__((rvp4_action))
void action_forward(void) { }

__attribute__((rvp4_action))
void action_drop(void) { }

// 声明表（固定到 Stage 0，LPM，最大 4096 条）
__attribute__((rvp4_table))
__attribute__((rvp4_lpm))
__attribute__((rvp4_stage(0)))
__attribute__((rvp4_size(4096)))
void table_ipv4_lpm(void) { }
```

### 10.3 编译报告示例

```
RV-P4 C-to-HW Compiler Report
========================================
Source: example_dataplane.c
Tables: 4
Actions: 16
Parser states: 7

Table Allocation:
  [ 0] table_ipv4_lpm      lpm      size=65536
  [ 1] table_acl_ingress   ternary  size=4096
  [ 2] table_l2_fdb        exact    size=32768
  [ 3] table_qos_mark      exact    size=64

Actions:
  0x1001  forward
  0x1002  drop
  ...
  0x5000  action_forward
  ...

Resource Usage:
  MAU stages used: 4/24
  Parser TCAM entries: 7/256
```

---

## 11. 已知局限

1. **表键字段未完全解析**：`TableDef.key_fields` 和 `actions` 字段当前为空列表，编译器尚未从函数体中提取 `key{}` / `actions{}` 块，这部分逻辑是预留扩展点。

2. **Parser 状态跳转固定**：`_register_parser_state()` 目前为所有用户状态生成固定模板（以太网帧头解析），不解析 C 函数体中的 `if/switch` 跳转逻辑。

3. **每动作仅一个原语**：`encode_action_params()` 只编码 `primitives[0]`，多原语动作尚不支持（硬件最多 8 个原语/动作）。

4. **MAU TCAM 镜像全零**：`gen_mau_tcam_bin()` 输出全零镜像（所有条目无效），运行时依赖控制面固件通过 TUE 写入具体条目。

5. **无 PHV 字段自动推断**：PHV 字段偏移表（`PHV_FIELDS`）硬编码在编译器中，不从 `rvp4_phv_field` 标注自动推算。
