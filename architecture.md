# RISC-V P4 Switch ASIC 架构设计文档

**版本**: v1.0
**日期**: 2026-02-19
**状态**: 正式发布

---

## 目录

1. [系统概述](#1-系统概述)
2. [顶层架构图](#2-顶层架构图)
3. [模块层次结构](#3-模块层次结构)
4. [各模块接口定义](#4-各模块接口定义)
5. [关键数据结构](#5-关键数据结构)
6. [快速路径数据流](#6-快速路径数据流)
7. [慢速路径数据流](#7-慢速路径数据流)
8. [模块详细说明](#8-模块详细说明)
9. [时钟域划分](#9-时钟域划分)
10. [复位策略](#10-复位策略)
11. [关键时序约束](#11-关键时序约束)

---

## 1. 系统概述

RV-P4 是一款 32 端口以太网交换 ASIC，将 P4 可编程数据面与基于香山（XiangShan Nanhu）的 64 位 RISC-V 控制面集成于同一芯片。数据面以高达 1.6 GHz 的时钟频率处理报文，支持灵活的 P4 语义匹配动作（Match-Action）；控制面运行 C 语言固件，通过 APB 总线对数据面流表进行原子更新。

### 1.1 设计目标

| 指标 | 目标值 |
|------|--------|
| 端口数 | 32 × SerDes（全双工） |
| 单端口速率 | 支持多速率（由 SerDes 层决定） |
| 数据面时钟 | 1.6 GHz（clk_dp） |
| PHV 宽度 | 4096 位（512 字节） |
| 匹配动作级数 | 24 级 MAU 流水线 |
| 流表规格（每级） | 2048 条 TCAM + 65536 条 Action SRAM |
| 包缓冲容量 | 64 MiB（1M × 64 字节 Cell） |
| 控制面 CPU | XiangShan Nanhu 64 位 RISC-V，1.5 GHz |
| 表更新延迟 | 约 36 个 clk_ctrl 周期（原子更新） |

### 1.2 主要特性

- **P4 可编程**：解析器 FSM 状态转移由 256 × 640 位 TCAM 控制，可在不重新编译 RTL 的情况下通过固件更新报文解析规则。
- **24 级 MAU 流水线**：每级均含 TCAM 匹配 + Action SRAM 读取 + ALU 执行，全流水吞吐为 1 PHV/周期。当前固件占用前 7 级（Stage 0–6）实现 IPv4 LPM 路由、ACL、L2 FDB、ARP Punt、VLAN 入/出口处理和 DSCP→QoS 映射。
- **原子流表更新（TUE）**：shadow-write + pointer-swap 机制保证数据面在表更新期间不中断，避免读写竞争。
- **RISC-V 控制面**：香山核运行 C 固件（route.c / acl.c / fdb.c 等），通过 MMIO 调用 HAL API（hal_tcam_insert 等），经 APB 总线驱动 TUE 完成流表编程。
- **Verilator 协同仿真**：tb/cosim/ 提供 C++ harness，将固件与 RTL 联合仿真，验证控制面流表下发与数据面转发的端到端正确性。

---

## 2. 顶层架构图

```
                          ┌─────────────────────────────────────────────────────────┐
                          │                      rv_p4_top                          │
                          │                                                         │
  32×SerDes RX ──────────►│  mac_rx_arb   ┌──────────┐  phv_bus[0]                 │
  (clk_mac)               │  (Round-Robin)│ p4_parser│──────────────────────────── │──►
                          │  32→1 仲裁   └──────────┘                             │
                          │       │                                                 │
                          │       │cell_alloc                                       │
                          │       ▼                                                 │
                          │  ┌────────────┐  64B cell                               │
                          │  │ pkt_buffer │◄──────────── Parser 写入                │
                          │  │ (1M cells) │──────────── TM 读取                    │
                          │  └────────────┘◄──────────── TM cell_free              │
                          │                                                         │
                          │  phv_bus[0..24]:                                        │
                          │  ┌─────────┐  ┌─────────┐       ┌─────────┐            │
                          │  │mau_stage│  │mau_stage│  ...  │mau_stage│            │
                          │  │ [0]     │─►│ [1]     │──────►│ [23]    │            │
                          │  └─────────┘  └─────────┘       └─────────┘            │
                          │       ▲              ▲                 ▲                │
                          │       │        mau_cfg[i]（来自 TUE）  │                │
                          │  ┌─────────────────────────────────────────────────┐   │
                          │  │                     tue                         │   │
                          │  │  APB slave ← ctrl_plane                        │   │
                          │  │  clk_ctrl 域 FSM + 2-FF同步 → clk_dp           │   │
                          │  └─────────────────────────────────────────────────┘   │
                          │                                                         │
                          │  phv_bus[24]                                            │
                          │       │                                                 │
                          │       ▼                                                 │
                          │  ┌─────────────────┐                                   │
                          │  │ traffic_manager  │──► 32×SerDes TX (clk_dp)         │
                          │  │ (DWRR+SP, 256Q) │                                   │
                          │  └─────────────────┘                                   │
                          │                                                         │
                          │  ┌─────────────────────────────────────────────────┐   │
                          │  │               ctrl_plane                        │   │
                          │  │  XiangShan(clk_cpu) + PCIe(pcie_clk)           │   │
                          │  │  APB master (clk_ctrl) → apb_bus[16]           │   │
                          │  └─────────────────────────────────────────────────┘   │
                          │                                                         │
  PCIe(256b) ────────────►│  ctrl_plane                                            │
  JTAG ──────────────────►│  ctrl_plane                                            │
  tb_parser_wr_* ────────►│  p4_parser (backdoor)                                  │
  tb_tue_* ──────────────►│  tue       (backdoor)                                  │
                          └─────────────────────────────────────────────────────────┘
```

### 2.1 快速路径（Fast Path）总览

```
MAC RX cell (clk_mac)
       │
       ▼
  mac_rx_arb          ← 轮询仲裁，保证帧内连续输出
       │ mac_rx_if (clk_mac)
       ▼
  p4_parser           ← TCAM FSM 逐字段提取 → PHV
       │ pb_wr_if     → pkt_buffer（原始 cell 写入）
       │ phv_bus[0]   (clk_dp, valid/ready 握手)
       ▼
  mau_stage[0..23]    ← 24 级 crossbar→TCAM→ASRAM→ALU
       │ phv_bus[24]
       ▼
  traffic_manager     ← PHV meta.eg_port 决定出端口
       │               读 pkt_buffer，驱动 MAC TX
       ▼
MAC TX cell (clk_dp)
```

---

## 3. 模块层次结构

```
rv_p4_top
├── rst_sync          × 3  (rst_dp_n / rst_ctrl_n / rst_cpu_n)
├── mac_rx_arb             (clk_mac)
├── p4_parser              (clk_dp)
│   └── parser_tcam        (clk_dp)
├── mau_stage[0..23]       (clk_dp)  [generate × 24]
│   ├── mau_tcam
│   ├── mau_hash
│   └── mau_alu
├── traffic_manager        (clk_dp)
├── pkt_buffer             (clk_dp)
├── tue                    (clk_ctrl + clk_dp)
└── ctrl_plane             (clk_cpu + clk_ctrl)
    └── xiangshan_nanhu_core  (clk_cpu，Chisel 生成黑盒)
```

### 3.1 RTL 文件清单

| 文件路径 | 模块名 | 所属时钟域 |
|----------|--------|------------|
| `rtl/top/rv_p4_top.sv` | `rv_p4_top` | 顶层 |
| `rtl/common/rst_sync.sv` | `rst_sync` | 各域 |
| `rtl/common/mac_rx_arb.sv` | `mac_rx_arb` | clk_mac |
| `rtl/parser/p4_parser.sv` | `p4_parser` | clk_dp |
| `rtl/parser/parser_tcam.sv` | `parser_tcam` | clk_dp |
| `rtl/mau/mau_stage.sv` | `mau_stage` | clk_dp |
| `rtl/mau/mau_tcam.sv` | `mau_tcam` | clk_dp |
| `rtl/mau/mau_alu.sv` | `mau_alu` | clk_dp |
| `rtl/mau/mau_hash.sv` | `mau_hash` | clk_dp |
| `rtl/tue/tue.sv` | `tue` | clk_ctrl + clk_dp |
| `rtl/pkt_buffer/pkt_buffer.sv` | `pkt_buffer` | clk_dp |
| `rtl/tm/traffic_manager.sv` | `traffic_manager` | clk_dp |
| `rtl/deparser/deparser.sv` | `deparser` | clk_dp |
| `rtl/ctrl/ctrl_plane.sv` | `ctrl_plane` | clk_cpu + clk_ctrl |
| `rtl/include/rv_p4_pkg.sv` | package `rv_p4_pkg` | — |
| `rtl/include/rv_p4_if.sv` | 所有 interface | — |

---

## 4. 各模块接口定义

### 4.1 顶层端口（rv_p4_top）

| 信号组 | 方向 | 宽度 | 说明 |
|--------|------|------|------|
| `clk_dp` | in | 1 | 数据面主时钟，1.6 GHz |
| `clk_ctrl` | in | 1 | 控制面时钟，200 MHz |
| `clk_cpu` | in | 1 | CPU 时钟，1.5 GHz |
| `clk_mac` | in | 1 | MAC/PCS 时钟，390.625 MHz |
| `rst_n` | in | 1 | 全局异步复位（低有效），同步释放 |
| `rx_valid[31:0]` | in | 32 | 各端口 RX cell 有效 |
| `rx_sof[31:0]` | in | 32 | 帧起始标志 |
| `rx_eof[31:0]` | in | 32 | 帧结束标志 |
| `rx_eop_len[31:0][6:0]` | in | 32×7 | EOF cell 有效字节数（1–64） |
| `rx_data[31:0][511:0]` | in | 32×512 | 64B cell 数据 |
| `rx_ready[31:0]` | out | 32 | 背压信号 |
| `tx_valid/sof/eof/eop_len/data` | out | — | TX 对应信号，结构与 RX 对称 |
| `tx_ready[31:0]` | in | 32 | SerDes TX 背压 |
| `pcie_clk` | in | 1 | PCIe 时钟 |
| `pcie_rx_data[255:0]` | in | 256 | PCIe RX（固件加载/管理） |
| `pcie_tx_data[255:0]` | out | 256 | PCIe TX |
| `pcie_rx_valid / pcie_tx_valid` | in/out | 1 | PCIe 有效 |
| `tck/tms/tdi/tdo` | in/out | 1 | JTAG |
| `tb_parser_wr_en/addr/data` | in | — | Parser TCAM 直写后门（仿真用） |
| `tb_tue_paddr/pwdata/psel/penable/pwrite` | in | — | TUE APB 直连后门（仿真用） |
| `tb_tue_prdata/pready` | out | — | TUE APB 读数据 |

### 4.2 SystemVerilog Interface 定义

#### phv_if — PHV 总线

```
interface phv_if (input logic clk, input logic rst_n);
    logic               valid;           // 数据有效
    logic [4095:0]      data;            // PHV 数据（512 字节）
    phv_meta_t          meta;            // 元数据（端口、drop、QoS 等）
    logic               ready;           // 接收方背压
    modport src (output valid, data, meta; input ready);
    modport dst (input  valid, data, meta; output ready);
endinterface
```

#### apb_if — APB 控制总线

```
interface apb_if (input logic clk, input logic rst_n);
    logic        psel;       // 从设备选择
    logic        penable;    // 使能（访问阶段）
    logic        pwrite;     // 1=写，0=读
    logic [11:0] paddr;      // 地址（4 KB 地址空间/从设备）
    logic [31:0] pwdata;     // 写数据
    logic [31:0] prdata;     // 读数据
    logic        pready;     // 从设备就绪（0=插入等待态）
    logic        pslverr;    // 从设备错误
    modport master (...);
    modport slave  (...);
endinterface
```

APB 总线共 16 个从设备槽（`apb_bus[0..15]`），地址映射如下：

| 槽号 | 基地址 | 从设备 |
|------|--------|--------|
| 0 | 0xA000_0000 | Parser CSR |
| 1 | 0xA000_1000 | Traffic Manager CSR |
| 2 | 0xA000_2000 | TUE CSR |
| 3 | 0xA000_3000 | 预留 |
| 4 | 0xA000_4000 | Packet Buffer CSR |
| 5–15 | — | 预留 |

#### mac_rx_if — MAC RX cell 流

```
interface mac_rx_if (input logic clk, input logic rst_n);
    logic [4:0]   port;      // 源端口号（0–31）
    logic         valid;     // cell 有效
    logic         sof;       // 帧起始 cell
    logic         eof;       // 帧结束 cell
    logic [6:0]   eop_len;   // EOF cell 有效字节数（1–64）
    logic [511:0] data;      // 64B cell 数据
    logic         ready;     // 背压
endinterface
```

#### mac_tx_if — MAC TX cell 流

结构与 `mac_rx_if` 对称，port 字段为目的端口号。

#### pb_wr_if — 包缓冲写端口（Parser → pkt_buffer）

| 信号 | 宽度 | 说明 |
|------|------|------|
| `valid` | 1 | cell 写入有效 |
| `cell_id[19:0]` | 20 | 目标 cell ID（由 free list 分配） |
| `data[511:0]` | 512 | 64B cell 数据 |
| `sof` | 1 | 帧首 cell |
| `eof` | 1 | 帧尾 cell |
| `data_len[6:0]` | 7 | 本 cell 有效字节数 |
| `ready` | 1 | 写口就绪（pkt_buffer 侧，常为 1） |

#### pb_rd_if — 包缓冲读端口（TM/Deparser → pkt_buffer）

| 信号 | 宽度 | 方向 | 说明 |
|------|------|------|------|
| `req_valid` | 1 | master→slave | 读请求有效 |
| `req_cell_id[19:0]` | 20 | master→slave | 待读 cell ID |
| `req_ready` | 1 | slave→master | 常为 1（无背压） |
| `rsp_data[511:0]` | 512 | slave→master | 读出的 cell 数据 |
| `rsp_valid` | 1 | slave→master | 响应有效（1 周期延迟） |
| `rsp_next_cell_id[19:0]` | 20 | slave→master | 链表下一 cell ID |
| `rsp_eof` | 1 | slave→master | 该 cell 为帧尾 |

#### cell_alloc_if — Cell 分配/释放

| 信号 | 方向（requester） | 说明 |
|------|------------------|------|
| `alloc_req` | 输出 | 申请一个空闲 cell |
| `alloc_id[19:0]` | 输入 | 分配到的 cell ID |
| `alloc_valid` | 输入 | 分配成功（free list 非空） |
| `alloc_empty` | 输入 | 缓冲耗尽告警 |
| `free_req` | 输出 | 释放一个 cell |
| `free_id[19:0]` | 输出 | 待释放 cell ID |

#### tue_req_if — TUE 请求接口

| 信号 | 说明 |
|------|------|
| `valid` | 请求有效 |
| `req (tue_req_t)` | 操作类型 + stage + key/mask/action |
| `ready` | TUE 空闲可接受请求 |
| `done` | 本次更新完成脉冲 |
| `error` | 错误标志 |

#### mau_cfg_if — TUE → MAU 配置接口（每级独立）

| 信号 | 宽度 | 说明 |
|------|------|------|
| `tcam_wr_en` | 1 | TCAM 写使能 |
| `tcam_wr_addr[10:0]` | 11 | TCAM 条目地址（0–2047） |
| `tcam_wr_key[511:0]` | 512 | 匹配键 |
| `tcam_wr_mask[511:0]` | 512 | 掩码（bit=1 → don't care） |
| `tcam_action_id[15:0]` | 16 | 动作 ID |
| `tcam_action_ptr[15:0]` | 16 | Action SRAM 索引 |
| `tcam_wr_valid` | 1 | 条目有效位 |
| `asram_wr_en` | 1 | Action SRAM 写使能 |
| `asram_wr_addr[15:0]` | 16 | SRAM 地址（0–65535） |
| `asram_wr_data[127:0]` | 128 | SRAM 数据 = {action_id[15:0], 16'b0, P2[31:0], P1[31:0], P0[31:0]} |

---

## 5. 关键数据结构

### 5.1 PHV 元数据（phv_meta_t）

```systemverilog
typedef struct packed {
    logic [4:0]            ig_port;     // 入端口（0–31）
    logic [4:0]            eg_port;     // 出端口（0–31，由 MAU 动作写入）
    logic                  drop;        // 丢包标志（OP_DROP 置 1）
    logic                  multicast;   // 组播标志
    logic                  mirror;      // 镜像标志
    logic [2:0]            qos_prio;    // QoS 优先级（0–7）
    logic [13:0]           pkt_len;     // 报文总长度（字节）
    logic [CELL_ID_W-1:0]  cell_id;     // 包缓冲首 cell ID（20b）
    logic [47:0]           timestamp;   // 入包时间戳（ns，48b）
    logic [31:0]           flow_hash;   // 5 元组哈希值
} phv_meta_t;  // 总计 ~136 位
```

### 5.2 MAU TCAM 条目（mau_tcam_entry_t）

```systemverilog
typedef struct packed {
    logic [511:0]  key;         // 512b 匹配键（与 PHV[511:0] 比较）
    logic [511:0]  mask;        // 512b 掩码（bit=1 → don't care）
    logic [15:0]   action_id;   // 动作类型，bit[15:12] = op_type
    logic [15:0]   action_ptr;  // Action SRAM 索引
    logic          valid;       // 条目有效位
} mau_tcam_entry_t;
```

**TCAM 匹配语义**：`((key XOR t_key) AND (NOT t_mask)) == 0`，即 mask=1 的位为 don't care，mask=0 的位必须精确匹配。

### 5.3 Action SRAM 条目（mau_action_t）

```systemverilog
typedef struct packed {
    logic [15:0]   action_id;   // 动作 ID（同 TCAM action_id）
    logic [111:0]  params;      // 动作参数（14 字节）
} mau_action_t;  // 共 128b，对应 MAU_ASRAM_WIDTH
```

**params 位域分配**（由 TUE 写入，格式 `{action_id[15:0], 16'b0, P2[31:0], P1[31:0], P0[31:0]}`）：

| 位段 | 含义 | 说明 |
|------|------|------|
| `[111:80]` | dst_offset | PHV 目标字节偏移（实际取低 10b） |
| `[79:48]` | src_offset | PHV 源字节偏移（实际取低 10b） |
| `[47:16]` | imm_value | 32b 立即数（OP_SET_PORT 时为出端口号） |
| `[15:8]` | field_width | 操作字段字节宽度（1/2/4/6/8） |
| `[7:0]` | 保留 | — |

### 5.4 ALU 动作编码（action_id[15:12] = op_type）

| op_type | 助记符 | 操作描述 |
|---------|--------|----------|
| 0x0 | OP_NOP | 无操作 |
| 0x1 | OP_SET | `PHV[dst] = imm_val`（按字段宽度） |
| 0x2 | OP_COPY | `PHV[dst] = PHV[src]` |
| 0x3 | OP_ADD | `PHV[dst] += imm_val` |
| 0x4 | OP_SUB | `PHV[dst] -= imm_val` |
| 0x5 | OP_AND | `PHV[dst] &= imm_val` |
| 0x6 | OP_OR | `PHV[dst] |= imm_val` |
| 0x7 | OP_XOR | `PHV[dst] ^= imm_val` |
| 0x8 | OP_SET_META | 修改 meta 字段 |
| 0x9 | OP_DROP | `meta.drop = 1` |
| 0xA | OP_SET_PORT | `meta.eg_port = imm_val[4:0]` |
| 0xB | OP_SET_PRIO | `meta.qos_prio = imm_val[2:0]` |
| 0xC | OP_HASH_SET | `PHV[dst] = hash_result`（CRC32） |
| 0xD | OP_COND_SET | `if PHV[src]!=0 then PHV[dst]=imm_val` |

### 5.5 TUE 请求结构（tue_req_t）

```systemverilog
typedef struct packed {
    tue_op_t                   op;           // INSERT/DELETE/MODIFY/FLUSH
    logic [4:0]                stage;        // 目标 MAU 级（0–23），0x1F=Parser
    logic [15:0]               table_id;     // TCAM 条目索引
    logic [511:0]              key;          // 匹配键
    logic [511:0]              mask;         // 掩码（RTL 约定：1=don't care）
    logic [15:0]               action_id;    // 动作 ID
    logic [111:0]              action_params;// 动作参数
} tue_req_t;
```

### 5.6 Parser TCAM 条目格式（640b）

| 位域 | 宽度 | 含义 |
|------|------|------|
| `[639:634]` | 6b | key_state（当前 FSM 状态，精确匹配） |
| `[633:570]` | 64b | key_window（8 字节窗口，与当前 cell 字节比较） |
| `[569:564]` | 6b | mask_state（state 掩码，0=精确匹配） |
| `[563:506]` | 58b | 填充 |
| `[505:442]` | 64b | mask_window（1=don't care） |
| `[441:436]` | 6b | next_state（下一 FSM 状态，0x3F=ACCEPT） |
| `[435:428]` | 8b | extract_offset（提取字节在 cell 内的偏移） |
| `[427:420]` | 8b | extract_len（提取长度，当前实现固定为 1） |
| `[419:410]` | 10b | phv_dst_offset（写入 PHV 的目标字节偏移） |
| `[409:402]` | 8b | hdr_advance（报头指针推进字节数） |
| `[401]` | 1b | valid（条目有效位） |
| `[400:0]` | 401b | 保留 |

### 5.7 PHV 字段偏移映射（table_map.h 约定）

| 字段 | PHV 字节偏移 | 长度 |
|------|-------------|------|
| 目的 MAC | 0 | 6B |
| 源 MAC | 6 | 6B |
| EtherType | 12 | 2B |
| VLAN TCI | 14 | 2B |
| IPv4 Ver+IHL | 18 | 1B |
| IPv4 DSCP/TOS | 19 | 1B |
| IPv4 Total Len | 20 | 2B |
| IPv4 TTL | 26 | 1B |
| IPv4 Proto | 27 | 1B |
| IPv4 SRC | 30 | 4B |
| IPv4 DST | 34 | 4B |
| TCP/UDP Sport | 38 | 2B |
| TCP/UDP Dport | 40 | 2B |
| 元数据：入端口 | 256 | 1B |
| 元数据：出端口 | 257 | 1B |
| 元数据：drop | 258 | 1B |
| 元数据：优先级 | 259 | 1B |
| 元数据：flow_hash | 260 | 4B |
| VLAN ID | 261 | 2B |
| QoS 优先级 | 263 | 1B |

### 5.8 TUE APB 寄存器映射

| 偏移 | 寄存器名 | 操作 | 说明 |
|------|---------|------|------|
| 0x000 | TUE_REG_CMD | W | 命令：0=INSERT，1=DELETE，2=MODIFY，3=FLUSH |
| 0x004 | TUE_REG_TABLE_ID | W | TCAM 条目索引（兼 Action SRAM 地址） |
| 0x008 | TUE_REG_STAGE | R/W | 目标 MAU 级（读：当前配置） |
| 0x010–0x04C | TUE_REG_KEY_0–15 | W | 512b 匹配键（16 × 32b） |
| 0x050–0x08C | TUE_REG_MASK_0–15 | W | 512b 掩码（16 × 32b） |
| 0x090 | TUE_REG_ACTION_ID | W | 动作 ID |
| 0x094 | TUE_REG_ACTION_P0 | W | 动作参数字 0（含出端口号等） |
| 0x098 | TUE_REG_ACTION_P1 | W | 动作参数字 1 |
| 0x09C | TUE_REG_ACTION_P2 | W | 动作参数字 2 |
| 0x0A0 | TUE_REG_STATUS | R | 状态：0=IDLE，1=BUSY，2=DONE，3=ERR |
| 0x0A4 | TUE_REG_COMMIT | W | 写 1 触发事务（自清） |

---

## 6. 快速路径数据流

快速路径（Fast Path）是报文从入端口 SerDes 到出端口 SerDes 的端到端处理路径，全程在 `clk_dp`（1.6 GHz）域内流水执行。

### 6.1 总体流程

```
步骤   模块              操作
────  ──────────────   ──────────────────────────────────────────────────
 1    mac_rx_arb        轮询 32 路 rx_valid，对有效 SOF 帧进行仲裁，
                        输出单路 mac_rx_if 到 p4_parser。
                        FSM：S_IDLE → S_GRANT（帧内锁定端口）→ S_EOF。
 2    p4_parser         接收 64B cell，按 TCAM FSM 逐字节提取报头字段：
       (PS_IDLE)        · SOF 到达，分配首 cell ID，锁存 cell。
       (PS_LATCH)       · 触发 TCAM 查找（1 周期）。
       (PS_TCAM_WAIT)   · 等待 TCAM 结果。
       (PS_PROCESS)     · TCAM 命中：提取 1B 到 phv_buf，推进 hdr_ptr，
                          转换到 next_state；若 next_state=0x3F（ACCEPT）
                          则报头解析完成。
                          TCAM miss：置 drop，丢弃帧。
       (PS_PAYLOAD)     · 继续接收 payload cell，更新 pkt_len。
       (PS_EMIT)        · 输出 PHV 到 phv_bus[0]（valid/ready 握手）。
 3    pkt_buffer        · 与 p4_parser 并行：Parser 将每个收到的 cell
                          写入 pkt_buffer（pb_wr_if），cell 按链表组织。
                          Free list FIFO 分配/回收 cell ID（CELL_ID_W=20b）。
 4    mau_stage[0..23]  · 每级 4 子级流水（4 clk_dp 周期延迟）：
                          子级 0：crossbar — 取 PHV[511:0] 作 match_key（512b）。
                          子级 1：mau_tcam — 2048 条并行匹配，输出命中 idx。
                          子级 2：asram — 读 Action SRAM（64K×128b），取 action。
                          子级 3：mau_alu — 执行动作，写回修改后的 PHV/meta。
                        · 24 级总延迟：96 clk_dp 周期（背压透传保证无气泡）。
 5    traffic_manager   · 从 phv_bus[24] 接收 PHV，按 meta.eg_port 入队
                          对应端口的 FIFO（深度 8）。
                        · drop=1 的报文直接丢弃，不写入 FIFO。
                        · TX 状态机：TX_IDLE → TX_READ（读 pkt_buffer）
                          → TX_SEND（按 cell 链表逐 cell 输出到 SerDes TX）
                          → TX_FREE（释放 cell）。
 6    deparser          · 从 PHV 中重建以太网/IPv4 报头（TTL 递减，
                          IPv4 checksum RFC-1624 增量更新），叠加到
                          首 cell 对应字节，输出到 MAC TX。
```

### 6.2 MAU 流水线各级功能分配

| Stage | 表名 | 匹配字段（PHV 偏移） | 默认动作 |
|-------|------|---------------------|---------|
| 0 | IPv4 LPM 路由 | IPv4 DST（byte 34，/16 最长前缀） | ACTION_DROP |
| 1 | ACL 入口 | IPv4 SRC（byte 30）+ 前缀掩码 | ACTION_PERMIT |
| 2 | L2 FDB | 目的 MAC（byte 0，精确匹配） | ACTION_FLOOD |
| 3 | ARP Punt | EtherType（byte 12，匹配 0x0806） | 透传 |
| 4 | VLAN 入口 | EtherType（byte 12），打标/剥标 | 透传 |
| 5 | DSCP→QoS | IPv4 TOS（byte 15），映射优先级 | 不修改 |
| 6 | VLAN 出口 | VLAN TCI（byte 14），trunk/access | 透传 |
| 7–23 | 预留 | — | OP_NOP |

### 6.3 包缓冲 Cell 链表管理

```
┌──────────┐   alloc_req   ┌─────────────────────────┐
│ p4_parser│──────────────►│         pkt_buffer       │
│          │◄──────────────│   free_list FIFO (1M)    │
│          │   alloc_id    │   cell_data [1M][512b]   │
│          │               │   cell_next [1M][20b]    │
│          │──── pb_wr ───►│   cell_eof  [1M][1b]     │
└──────────┘               └─────────────────────────┘
                                   ▲          │
                            free_req/id        │ pb_rd_tm（TM 读）
                            ┌──────────┐       │ pb_rd_dp（Deparser 读）
                            │traffic_  │◄──────┘
                            │manager   │
                            └──────────┘
```

Parser 分配 cell，按链表写入 cell_data；TM 通过 pb_rd_tm 按链表读取每个 cell 进行发送，发送完毕后通过 cell_free_if 将所有 cell 归还 free list。

---

## 7. 慢速路径数据流

慢速路径（Slow Path / Control Plane）负责流表的安装、更新与维护，由香山 RISC-V 核上运行的 C 固件驱动，通过 TUE 原子地将流表写入数据面 MAU。

### 7.1 流表更新流程

```
C 固件（clk_cpu，1.5 GHz）
    │
    │  route_add / fdb_add_static / acl_add_deny 等
    ▼
HAL 层（rv_p4_hal.c）
    │  hal_tcam_insert(entry)
    │
    │  1. 固件掩码约定转换（fw_mask: 1=must match → RTL mask: 1=don't care）
    │     rtl_mask = ~fw_mask
    │  2. Action ID 映射（ACTION_FORWARD→OP_SET_PORT, ACTION_DENY→OP_DROP 等）
    ▼
APB 总线（clk_ctrl，200 MHz）
    │  寄存器写入顺序：
    │  TUE_CMD → TUE_TABLE_ID → TUE_STAGE →
    │  TUE_KEY_0..15 → TUE_MASK_0..15 →
    │  TUE_ACTION_ID → TUE_ACTION_P0..P2 → TUE_COMMIT=1
    ▼
TUE 状态机（clk_ctrl 域）
    │  TS_IDLE → TS_WAIT_DRAIN（33 周期）→ TS_APPLY → TS_DONE
    │
    │  WAIT_DRAIN：等待 dp 流水线排空，避免写流水中的 PHV 读到新条目
    │  APPLY：拉高 apply_pulse_ctrl，锁存全部配置寄存器到 dp_* 信号
    │
    │  apply_pulse_ctrl → 2-FF 同步器 → apply_pulse_dp
    ▼
MAU 配置写入（clk_dp 域，apply_pulse_dp 触发）
    │  广播 mau_cfg[dp_stage]：
    │  · tcam_wr_en=1（本级）：写 TCAM key/mask/action_id/action_ptr/valid
    │  · asram_wr_en=1（本级）：写 Action SRAM[table_id]
    │    数据格式：{action_id[15:0], 16'b0, P2[31:0], P1[31:0], P0[31:0]}
    ▼
MAU TCAM/SRAM 更新完成
    │
    └── TUE 状态机回到 TS_IDLE，reg_status=DONE(2)
        HAL 返回 HAL_OK
```

**总更新延迟**：约 36 个 clk_ctrl 周期（1 IDLE + 33 WAIT_DRAIN + 1 APPLY + 1 DONE），加上 2 个 clk_dp 周期的跨域同步延迟，共约 300 ns。

### 7.2 Parser TCAM 更新

Parser TCAM 条目通过 stage=0x1F（保留给 Parser）触发，apply_pulse_dp 时 TUE 输出 parser_wr_en/addr/data 信号，直接写入 parser_tcam 的单端口 SRAM。

在协同仿真中，测试代码可绕过 TUE，直接通过 tb_parser_wr_* 后门端口写 Parser TCAM，以加速仿真初始化。

### 7.3 ARP Punt 路径

ARP 报文通过 Stage 3 TCAM（匹配 EtherType=0x0806）命中后，触发 ACTION_PUNT_CPU 动作：报文头部通过 Punt 环（HAL_BASE_PUNT，MMIO 共享环）递送至 CPU；固件通过 `hal_punt_rx_poll()` 轮询接收，处理完毕后可调用 `hal_punt_tx_send()` 注入回包。

### 7.4 PCIe 固件加载

系统上电后，外部主机通过 256b PCIe 接口将 C 固件二进制映像传输给 ctrl_plane，ctrl_plane 解析 PCIe 包（bit[255]=写使能，bit[254:235]=地址，bit[31:0]=数据），通过 APB 总线将代码/数据写入香山核的指令/数据 SRAM，随后复位香山核使其执行固件。

---

## 8. 模块详细说明

### 8.1 mac_rx_arb — MAC RX 仲裁器

**功能**：将 32 路独立 MAC RX cell 流汇聚为单路，供 p4_parser 顺序处理。

**算法**：轮询（Round-Robin），仲裁指针 `rr_ptr` 在每次仲裁后递进，保证长期公平性。帧内不切换端口（S_GRANT 状态持续到当前帧 EOF），确保同一报文的所有 cell 连续输出，不被其他端口帧打断。

**FSM**：

```
S_IDLE ──(any_valid && rx_sof)──► S_GRANT  (锁定 grant_port，更新 rr_ptr)
S_GRANT ──(out.valid && out.eof)──► S_IDLE
```

**输出**：`out` 为 `mac_rx_if.src`，连接到 `p4_parser.rx`，时钟为 `clk_mac`（390.625 MHz）。输出在顶层由 p4_parser 在 clk_dp 域锁存，实现 MAC→DP 跨时钟域处理。

### 8.2 p4_parser — P4 可编程解析器

**功能**：从 64B cell 中按 FSM 逐字段提取报头字节，构建 PHV；同时将原始 cell 写入 pkt_buffer。

**TCAM 驱动的 FSM**：初始状态为 `fsm_state=1`（ST_ETHERNET）。每次在当前 cell 的 `hdr_ptr` 位置取 8B 字节窗口，与当前状态一并输入 parser_tcam（256 条 × 640b），1 周期出结果：命中则提取 1B 到 PHV 对应偏移，推进 hdr_ptr，切换 next_state；未命中则丢弃报文（drop=1）。next_state=0x3F 为 ACCEPT，触发 PHV 输出。

**解析器状态机**：

```
PS_IDLE → PS_LATCH → PS_TCAM_WAIT → PS_PROCESS
              ▲               (命中，继续) ─────────────┘
              │               (命中，ACCEPT) ────► PS_PAYLOAD ─(EOF)─► PS_EMIT
              │               (miss) ──────────────────────────────────► PS_DROP
```

**Cell 分配**：在 PS_IDLE 收到 SOF 时向 cell_alloc 申请首 cell；在 PS_PAYLOAD 收到非 EOF cell 时继续申请。free_req 恒为 0（Parser 只分配不释放）。

**PHV 输出**：PS_EMIT 状态下，`phv_out.valid=1`，携带完整 PHV 数据和 meta（ig_port、cell_id、pkt_len 等），通过 phv_bus[0] 握手送入 mau_stage[0]。

### 8.3 parser_tcam — 解析器状态转移 TCAM

**规格**：256 条目 × 640b，组合逻辑并行匹配，最低索引优先，1 周期流水延迟。

**匹配逻辑**：
```
state_match  = &((lookup_state  XNOR k_state)  | m_state)
window_match = &((lookup_window XNOR k_window) | m_window)
hit[i] = valid[i] && state_match && window_match
```

**配置接口**：支持 TUE 通过 parser_wr_en/addr/data 直写（640b/条目），也支持仿真时通过 tb_parser_wr_* 后门写入。

### 8.4 mau_stage — 匹配动作单元（单级）

每级 mau_stage 包含 4 流水子级，总吞吐 1 PHV/clk_dp，总延迟 4 拍：

| 子级 | 模块 | 操作 |
|------|------|------|
| 0 | crossbar | 从 PHV[511:0] 提取 512b match_key（当前实现直接取低 512b） |
| 1 | mau_tcam | 2048 条并行匹配，1 拍出 hit/action_id/action_ptr |
| 2 | asram | 同步读 Action SRAM（64K×128b），1 拍出 action 参数 |
| 3 | mau_alu | 执行 ALU 操作，修改 PHV 或 meta，寄存输出 |

并行执行的 mau_hash（CRC32）在子级 1 同步启动，子级 2 结果可用，供 OP_HASH_SET 操作使用。

背压处理：`phv_in.ready = phv_out.ready`（背压直通），上游在 phv_out 阻塞时停止发送。

### 8.5 mau_tcam — MAU 级 TCAM

**规格**：2048 条目 × 512b key+mask，组合逻辑并行匹配，最低索引优先，1 周期流水延迟。

**掩码约定**（与 Parser TCAM 方向相同）：`t_mask[i]=1` → don't care，`t_mask[i]=0` → 必须匹配。

配置写口来自 mau_cfg_if.receiver（TUE 驱动）。

### 8.6 mau_alu — 动作执行 ALU

纯组合逻辑（内部 `always_comb`），对输入 PHV/meta 执行动作，结果在 `clk_dp` 上升沿寄存输出。支持表 5.4 所列全部操作类型。OP_DROP 仅置 meta.drop=1，PHV 数据不清除；TM 在入队时检查 drop 位，丢弃对应报文。

### 8.7 pkt_buffer — 共享包缓冲

**存储**：
- `cell_data[1M][512b]`：64 MiB 包数据（综合时映射为片外 SRAM 宏）
- `cell_next[1M][20b]`：cell 链表（每个 cell 存储下一 cell ID）
- `cell_eof[1M][1b]`：帧尾标志
- `free_list[1M]`：空闲 cell ID FIFO（上电初始化全部填入）

**读写端口**：
- Port 0（写）：Parser 写入，单端口，无背压（wr.ready=1）
- Port 1（读）：TM 读取，1 周期延迟
- Port 2（读）：Deparser 读取，1 周期延迟

**Cell 分配**：`alloc_id = free_list[fl_head]`，每次 alloc_req 有效时 fl_head 递进；释放时 free_id 入队 free_list 尾部。

### 8.8 traffic_manager — 流量管理器

**功能**：从 phv_bus[24] 接收最终 PHV，根据 meta.eg_port 将报文描述符写入对应端口的 FIFO（深度 8），调度 MAC TX 发送。

**队列**：每端口 1 个 FIFO，共 32 个（对应 32 个 TX 端口）。描述符包含 cell_id、pkt_len、qos_prio、drop 标志。

**调度**：当前实现为严格优先级（端口 0 最高优先级）轮询 pkt_buffer 读请求。完整实现中支持 DWRR+SP，256 个逻辑队列（32 端口 × 8 优先级）。

**TX 状态机**（每端口）：
```
TX_IDLE → TX_READ（发起 pb_rd 请求）→ TX_SEND（输出 cell 到 SerDes TX）
       → TX_FREE（通过 cell_free_if 归还 cell）→ TX_IDLE
```

### 8.9 tue — 表更新引擎

**职责**：接收控制面发来的流表更新请求（APB 写或 tue_req_if），等待数据面流水线排空后，原子地将新条目写入对应 MAU 的 TCAM 和 Action SRAM，保证数据面连续性。

**事务状态机**（clk_ctrl 域）：

```
TS_IDLE ──(reg_commit=1)──► TS_WAIT_DRAIN (drain_cnt=32, 逐拍递减)
TS_WAIT_DRAIN ──(cnt==0)──► TS_APPLY (锁存 dp_* 信号，拉高 apply_pulse_ctrl)
TS_APPLY ──────────────────► TS_DONE (reg_status=DONE)
TS_DONE ───────────────────► TS_IDLE
```

**跨时钟域同步**：apply_pulse_ctrl（clk_ctrl 域）经 2-FF 同步器传递到 apply_pulse_dp（clk_dp 域），触发 MAU TCAM/SRAM 写入。配置数据（dp_stage/dp_key/dp_mask/dp_action_id/dp_action_params）在 TS_WAIT_DRAIN 末尾一拍预先锁存，确保在 apply_pulse_dp 到达时数据已稳定。

**广播机制**：通过 generate 展开，所有 24 级 mau_cfg_if 的配置信号由 TUE 广播驱动，仅 dp_stage 匹配的那一级的 tcam_wr_en/asram_wr_en 被置高。

**Parser 更新**：stage=0x1F 时触发 parser_wr_en，将 dp_key[7:0] 作为 Parser TCAM 地址，dp_key 作为写数据，更新 Parser TCAM 条目。

### 8.10 ctrl_plane — 控制面

**组成**：
- 香山核（`xiangshan_nanhu_core`）：Chisel 生成的 64 位 RISC-V 处理器黑盒，运行 C 固件；仿真时输出恒为 0，综合时链接 `XSTop.v`。
- PCIe 接口：接收 256b 数据包（bit[255]=写使能，bit[254:235]=地址，bit[31:0]=数据），用于外部主机写 MMIO 空间（固件加载、调试）。
- APB 主接口：将香山核的 MMIO 访问（TileLink→AXI4→APB 桥）转换为 16 个 APB 从设备上的读写操作，按地址高 4b（paddr[15:12]）选择从设备。

**MMIO 地址空间**（香山核视角）：

| 地址范围 | 模块 |
|----------|------|
| 0xA000_0000–0xA000_0FFF | Parser CSR |
| 0xA000_1000–0xA000_1FFF | Traffic Manager CSR |
| 0xA000_2000–0xA000_2FFF | TUE CSR |
| 0xA000_3000–0xA000_3FFF | 预留 |
| 0xA000_4000–0xA000_4FFF | Packet Buffer CSR |
| 0xA000_5000–0xA000_5FFF | VLAN CSR |
| 0xA000_6000–0xA000_6FFF | QoS CSR |
| 0xA000_7000–0xA000_7FFF | Punt 环 CSR |
| 0xA000_9000–0xA000_9FFF | UART CSR |

### 8.11 deparser — 解封装器

**功能**：将 MAU 流水线输出的 PHV 与 pkt_buffer 中的原始 payload 合并，重建完整的以太网帧。

**头部重建**（首 cell，tx_is_sof=1 时）：
- 将 PHV 中解析并可能被 MAU 修改的以太网/IPv4 头字段写回 cell 的对应字节位置（目的 MAC、源 MAC、EtherType、IPv4 头部字段等）。
- TTL 递减：从 PHV 取 IPv4 TTL，减 1 写回首 cell。
- IPv4 校验和：使用 RFC-1624 增量更新算法，基于旧 TTL 字、新 TTL 字、旧校验和计算新校验和，写入首 cell 对应位置。

**发送状态机**（每端口，同 TM）：TX_IDLE → TX_HDR → TX_READ → TX_SEND → TX_FREE。

---

## 9. 时钟域划分

芯片共有 4 个时钟域，各自承担不同功能：

| 时钟域 | 频率 | 复位信号 | 覆盖模块 |
|--------|------|---------|---------|
| clk_dp | 1.6 GHz | rst_dp_n | p4_parser、mau_stage[0..23]、mau_tcam、mau_alu、mau_hash、pkt_buffer、traffic_manager、deparser、TUE（dp 侧信号） |
| clk_ctrl | 200 MHz | rst_ctrl_n | tue（ctrl 侧 FSM 和 APB 寄存器）、ctrl_plane（APB master）、apb_bus |
| clk_cpu | 1.5 GHz | rst_cpu_n | xiangshan_nanhu_core |
| clk_mac | 390.625 MHz | rst_dp_n | mac_rx_arb |
| pcie_clk | PCIe 参考 | rst_ctrl_n | ctrl_plane（PCIe 接口逻辑） |

### 9.1 时钟频率关系

```
clk_dp   : clk_ctrl = 8  : 1
clk_dp   : clk_mac  ≈ 4  : 1
clk_dp   : clk_cpu  ≈ 1.07 : 1（异步，不同步）
```

在协同仿真中，clk_cpu 以 clk_dp 同相模拟（简化），clk_ctrl 每 8 个 dp 半周期切换一次，clk_mac 每 4 个 dp 半周期切换一次。

### 9.2 跨时钟域（CDC）处理

| 跨越方向 | 路径 | 处理方法 |
|---------|------|---------|
| clk_mac → clk_dp | mac_rx_arb 输出（mac_rx_if）→ p4_parser 输入 | p4_parser 在 clk_dp 上升沿锁存 mac_rx_if 信号（mac_rx_arb 输出在 mac_rx_if 寄存器中稳定） |
| clk_ctrl → clk_dp | TUE apply_pulse_ctrl → apply_pulse_dp | 2-FF 同步器（双寄存器链），属性 `ASYNC_REG="TRUE"` |
| clk_ctrl → clk_dp | TUE 配置数据（dp_key/mask/action/stage） | 在 apply_pulse_ctrl 脉冲前一拍锁存，数据在同步器传播期间保持稳定（满足建立/保持时间要求） |
| clk_dp → clk_ctrl | 无（TM 读 pkt_buffer 在 clk_dp 域内完成） | — |

**CDC 风险缓解**：

1. apply_pulse_ctrl 为单周期脉冲，经 2-FF 同步后在 clk_dp 域变为 apply_pulse_dp。由于 clk_ctrl/clk_dp 频率比约为 1:8，脉冲宽度（1 个 clk_ctrl 周期 = 8 个 clk_dp 周期）远大于 2-FF 同步器所需的 2 个 clk_dp 周期，不存在脉冲丢失风险。
2. 配置数据在 TS_WAIT_DRAIN 末尾锁存于 dp_* 寄存器，dp_* 寄存器由 clk_ctrl 驱动（当 apply_pulse_ctrl=1 时写入）。综合/STA 工具需将 dp_* → mau_cfg 的路径标注为多周期路径（multicycle_path，松弛 N 个 clk_dp 周期）。

### 9.3 时钟域架构图

```
                 clk_mac (390 MHz)
                      │
              ┌───────┴──────┐
              │  mac_rx_arb  │
              └───────┬──────┘
                      │ mac_rx_if（不跨域，p4_parser 在 clk_dp 锁存）
                      ▼
                 clk_dp (1.6 GHz)
                      │
        ┌─────────────┼────────────────────────────────┐
        │             │                                │
  p4_parser      mau_stage                    pkt_buffer
                 [0..23]                      traffic_manager
                                              deparser
                      │
                      │ apply_pulse_dp (2-FF 同步)
                      │◄──────────────────────────────────────┐
                      │                                        │
                 clk_ctrl (200 MHz)                   apply_pulse_ctrl
                      │                                        │
              ┌───────┴──────┐                         ┌──────┴──────┐
              │  ctrl_plane  │──── APB ────────────────►│     tue     │
              │ (APB master) │                          │  (ctrl FSM) │
              └───────┬──────┘                          └─────────────┘
                      │
                 clk_cpu (1.5 GHz)
                      │
              ┌───────┴──────┐
              │ XiangShan    │
              │ RISC-V Core  │
              └──────────────┘
```

---

## 10. 复位策略

### 10.1 复位设计原则

- **异步断言，同步释放**：外部 `rst_n`（低有效）可在任意时刻断言，立即拉低各域复位信号；释放时经 2-FF 同步器，在各自时钟域的上升沿同步地释放，避免亚稳态。
- **各时钟域独立复位**：每个时钟域有独立的同步复位信号，防止不同域之间的复位时序差异导致电路处于不确定状态。

### 10.2 rst_sync 实现

```systemverilog
module rst_sync (
    input  logic clk,
    input  logic rst_async_n,   // 异步复位输入
    output logic rst_sync_n     // 同步复位输出
);
    logic ff1, ff2;
    (* ASYNC_REG = "TRUE" *)    // 告知综合工具此 FF 用于 CDC，禁止优化
    always_ff @(posedge clk or negedge rst_async_n) begin
        if (!rst_async_n) {ff2, ff1} <= 2'b00;  // 异步断言：立即清零
        else              {ff2, ff1} <= {ff1, 1'b1};  // 同步释放：逐拍移入 1
    end
    assign rst_sync_n = ff2;    // 2 拍延迟后释放
endmodule
```

复位断言（rst_n=0）：两个 FF 立即清零，rst_sync_n 立即置 0（异步路径）。
复位释放（rst_n=1）：ff1 在下一个时钟上升沿置 1，ff2 再过一个上升沿置 1，rst_sync_n 延迟 2 个时钟周期后释放。

### 10.3 复位域分配

| 同步复位信号 | 时钟 | 覆盖模块 |
|------------|------|---------|
| `rst_dp_n` | clk_dp (1.6 GHz) | mac_rx_arb（复位时钟借用 clk_mac，但拉低方式相同）、p4_parser、parser_tcam、mau_stage[0..23]、mau_tcam、mau_alu、mau_hash、pkt_buffer、traffic_manager、deparser、TUE（apply_pulse 同步器 FF） |
| `rst_ctrl_n` | clk_ctrl (200 MHz) | tue（ctrl 侧 FSM 和 APB 寄存器）、ctrl_plane（APB master）、PCIe 接口逻辑 |
| `rst_cpu_n` | clk_cpu (1.5 GHz) | xiangshan_nanhu_core（通过 ~rst_cpu_n 连接到 reset 输入） |

注意：mac_rx_arb 使用 `clk_mac` 作为工作时钟，但其复位信号使用 `rst_dp_n`（在 clk_dp 同步释放）。由于 clk_mac 与 clk_dp 均来自同一 PLL 且频率关系固定，此处不会引入亚稳态问题；若需严格处理，应为 clk_mac 单独设置同步复位。

### 10.4 复位释放顺序

推荐的复位释放顺序（从硬复位到正常工作）：

```
1. 所有时钟稳定（PLL 锁定）
2. rst_n 断言期间：pkt_buffer 的 free_list 通过 initial 块预初始化
   （仿真时有效；综合时需上电序列或 ROM 初始化逻辑）
3. rst_n 释放 → rst_dp_n / rst_ctrl_n / rst_cpu_n 依次同步释放（各 2 个本域时钟周期后）
4. ctrl_plane（CPU）启动，固件初始化流表
5. TUE 接受第一批流表更新请求
6. Parser TCAM 加载完成，数据面开始处理报文
```

---

## 11. 关键时序约束

### 11.1 时钟约束

```tcl
# 主时钟定义（综合/STA 工具）
create_clock -period 0.625 -name clk_dp   [get_ports clk_dp]    ; # 1.6 GHz
create_clock -period 5.000 -name clk_ctrl [get_ports clk_ctrl]  ; # 200 MHz
create_clock -period 0.667 -name clk_cpu  [get_ports clk_cpu]   ; # 1.5 GHz
create_clock -period 2.560 -name clk_mac  [get_ports clk_mac]   ; # 390.625 MHz

# 将所有时钟声明为异步（无相位关系假设，除 dp 与 mac 关系已知外）
set_clock_groups -asynchronous \
    -group [get_clocks clk_dp] \
    -group [get_clocks clk_ctrl] \
    -group [get_clocks clk_cpu]
```

### 11.2 跨时钟域约束

```tcl
# 2-FF 同步器（TUE apply_pulse_ctrl → apply_pulse_dp）
# 声明为 false path（CDC 本身）：让 STA 不检查跨域组合路径
set_false_path -from [get_cells u_tue/apply_pulse_ctrl_reg] \
               -to   [get_cells u_tue/apply_pulse_dp_ff1_reg]

# dp_* 配置寄存器（TUE ctrl 域 → MAU dp 域）
# 声明为多周期路径：数据在 WAIT_DRAIN 期间已稳定，
# apply_pulse_dp 触发时距离数据锁存已过 ~33 clk_ctrl 周期
# 以 clk_dp 为参考，松弛 8 个周期（1 clk_ctrl = 8 clk_dp）
set_multicycle_path -setup 8 -end \
    -from [get_cells {u_tue/dp_key_reg[*] u_tue/dp_mask_reg[*] \
                      u_tue/dp_action_id_reg[*] u_tue/dp_action_params_reg[*] \
                      u_tue/dp_stage_reg[*]}] \
    -to   [get_cells {u_tue/gen_mau_cfg[*].mau_cfg*}]
set_multicycle_path -hold  7 -end \
    -from [get_cells {u_tue/dp_key_reg[*] ...}] \
    -to   [get_cells {u_tue/gen_mau_cfg[*].mau_cfg*}]
```

### 11.3 关键路径分析

| 路径 | 估算延迟 | 约束 | 说明 |
|------|---------|------|------|
| parser_tcam 组合匹配（256条并行） | ~0.4 ns | 0.625 ns（clk_dp 周期） | 256 条 XNOR+AND 树，输出寄存 |
| mau_tcam 组合匹配（2048条并行） | ~0.5 ns | 0.625 ns | 2048 条匹配树，是全片最关键路径 |
| mau_alu 组合逻辑 | ~0.3 ns | 0.625 ns | 简单 MUX/ALU，余量充足 |
| APB 写 → TUE 寄存器 | ~4.5 ns | 5.0 ns（clk_ctrl 周期） | 标准 APB 协议，无等待态 |
| mac_rx_arb 轮询逻辑 | ~2.0 ns | 2.56 ns（clk_mac 周期） | 32 路 OR 树 + 优先编码器 |

**MAU TCAM 是全片关键路径**：2048 条 512b TCAM 并行匹配需要在 0.625 ns 内完成。实际综合中需要：
- 将 TCAM 数组分割为多个 bank（如 4 × 512 条），分级比较后再优先编码；
- 采用专用 TCAM 硬宏（SRAM 基 TCAM IP）替代通用 SRAM 实现；
- 或将匹配拆分为 2 个流水级（每级 0.3 ns 内完成），增加 1 拍 MAU 延迟。

### 11.4 建立/保持时间裕量目标

| 时钟域 | 目标建立裕量 | 目标保持裕量 |
|--------|------------|------------|
| clk_dp（1.6 GHz） | ≥ 50 ps | ≥ 30 ps |
| clk_ctrl（200 MHz） | ≥ 200 ps | ≥ 100 ps |
| clk_cpu（1.5 GHz） | ≥ 60 ps | ≥ 30 ps |
| clk_mac（390 MHz） | ≥ 150 ps | ≥ 80 ps |

### 11.5 仿真与验证基础设施

**协同仿真框架**（`tb/cosim/`）：

```
tb/cosim/
├── cosim_main.cpp    # Verilator C++ 仿真 harness（HAL 实现 + 测试用例）
└── Makefile          # 编译脚本：verilator --cc --exe → obj_dir/Vrv_p4_top
```

**编译流程**：
```makefile
verilator --cc --exe                    \
  +incdir+$(INC_DIR)                    \
  --top-module rv_p4_top                \
  -Wno-MULTIDRIVEN -Wno-UNOPTFLAT ...  \
  cosim_main.cpp $(FW_SRCS) $(RTL_SRCS)
make -C obj_dir -f Vrv_p4_top.mk
```

**仿真时钟模型**（`step_half()` 函数）：

| 时钟 | 仿真半周期数（以 clk_dp 半周期为单位） |
|------|--------------------------------------|
| clk_dp | 1 |
| clk_cpu | 1（与 clk_dp 同相，简化） |
| clk_ctrl | 8 |
| clk_mac | 4 |

**后门端口**（仅用于仿真，生产中拉 0）：

| 后门端口组 | 对象 | 用途 |
|-----------|------|------|
| `tb_parser_wr_*` | parser_tcam | 直接写入 Parser TCAM 条目，绕过 TUE 加速初始化 |
| `tb_tue_*` | tue APB 接口 | 直接驱动 TUE 的 APB 总线，无需经由 ctrl_plane / 香山核 |

**RTL 协同仿真测试用例**：

| 测试编号 | 名称 | 验证内容 |
|---------|------|---------|
| CS-RTL-1 | IPv4 LPM routing | route_add → TUE 下发 → 报文转发到期望 TX 端口 |
| CS-RTL-2 | L2 FDB forwarding | fdb_add_static → TUE 下发 → 报文按 MAC 转发 |
| CS-RTL-3 | ACL deny | acl_add_deny → TUE 下发 → 报文被丢弃，无 TX 输出 |

**掩码转换约定**（固件 ↔ RTL）：
```
固件掩码：bit=1 → must match，bit=0 → don't care
RTL 掩码：bit=1 → don't care，bit=0 → must match
转换：rtl_mask = ~fw_mask（cosim_main.cpp: hal_tcam_insert 实现）
```

---

*本文档根据 RTL 源代码（`rtl/`）、软件固件（`sw/`）及协同仿真基础设施（`tb/cosim/`）自动推导生成，与代码实现保持一致。如发现差异，以 RTL 源码为准。*
