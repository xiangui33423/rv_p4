# RV-P4 Switch ASIC 设计规格文档

**版本**: v1.0
**日期**: 2026-02-18
**状态**: 正式草稿

---

## 目录

1. [系统概述](#一系统概述)
2. [顶层架构](#二顶层架构)
3. [数据面硬件模块设计](#三数据面硬件模块设计)
4. [Table Update Engine（TUE）](#四table-update-enginetue)
5. [数据面编程模型（C-to-HW编译器）](#五数据面编程模型c-to-hw编译器)
6. [控制面编程模型（RISC-V C SDK）](#六控制面编程模型risc-v-c-sdk)
7. [关键数据结构](#七关键数据结构)
8. [内部互联与时钟域](#八内部互联与时钟域)
9. [性能分析](#九性能分析)
10. [文件结构](#十文件结构)

---

# 一、系统概述

## 1.1 设计目标

RV-P4 Switch ASIC 是一款面向数据中心和骨干网络的高性能可编程交换芯片。其核心创新在于采用**统一C编程模型**：数据面与控制面均使用标准C语言描述，通过不同的编译路径分别映射到硬件流水线配置和RISC-V处理器指令。

设计目标如下：

| 目标项 | 指标 |
|--------|------|
| 线速转发能力 | 400 Mpps（64B小包） |
| 总带宽 | 12.8 Tbps（32×400GbE） |
| 端口配置 | 32×400GbE 或 128×100GbE |
| 数据面流水线延迟 | ≤ 1 μs（端到端，不含SerDes） |
| 表项规模 | TCAM 48K条目（24 stage × 2K） |
| 控制面处理器 | XiangShan RISC-V，1.5 GHz |
| 功耗目标 | ≤ 150 W（满负载） |
| 工艺节点 | 7nm FinFET |
| 芯片面积目标 | ≤ 400 mm² |
| 编程模型 | 统一C语言（数据面+控制面） |

## 1.2 性能指标详表

| 指标类别 | 参数 | 数值 | 备注 |
|----------|------|------|------|
| 吞吐量 | 最大包转发率 | 400 Mpps | 64B包，全双工 |
| 吞吐量 | 最大带宽 | 12.8 Tbps | 32端口×400G |
| 延迟 | Parser延迟 | 4 cycles | @1GHz = 4 ns |
| 延迟 | 单MAU Stage延迟 | 6 cycles | 含crossbar+TCAM+SRAM+ALU |
| 延迟 | 24×MAU总延迟 | 144 cycles | 144 ns |
| 延迟 | TM入队延迟 | 8 cycles | 8 ns |
| 延迟 | Deparser延迟 | 4 cycles | 4 ns |
| 延迟 | 端到端流水线延迟 | ~160 cycles | ~160 ns @1GHz |
| 表项 | TCAM总条目 | 48K | 24 stage × 2K |
| 表项 | Action SRAM总容量 | 24×64K×128b | 每stage 1MB |
| 表项 | 路由表（LPM） | 128K前缀 | 软件管理 |
| 表项 | ACL规则 | 48K | TCAM直接映射 |
| 表项 | L2 FDB | 256K MAC | Hash表 |
| 队列 | 总队列数 | 256 | 32端口×8队列 |
| 队列 | 每队列缓存 | 可配置 | 共享池+保证带宽 |
| 组播 | 组播组数 | 4K | 每组最多64成员 |
| 控制面 | CPU主频 | 1.5 GHz | XiangShan |
| 控制面 | 表更新速率 | ≥ 1M ops/s | TUE硬件加速 |
| 控制面 | 中断延迟 | ≤ 1 μs | 硬件中断到ISR |

## 1.3 统一C编程模型说明

RV-P4 的核心设计理念是让用户只需编写C代码，即可同时描述数据面转发逻辑和控制面管理逻辑，无需学习P4或其他专用语言。

### 1.3.1 数据面：编译时静态配置

```
用户C代码（数据面）
        │
        ▼
  RV-P4 C Frontend（基于LLVM）
        │  ├─ 语法检查（PARSER/TABLE/ACTION标注）
        │  ├─ 类型推断
        │  └─ 约束验证
        ▼
  中间表示（IR）
        │  ├─ 解析图（Parse Graph）
        │  ├─ 表依赖图（Table Dependency Graph）
        │  └─ 动作原语序列
        ▼
  硬件后端代码生成
        │  ├─ Parser FSM状态表（TCAM entry二进制）
        │  ├─ MAU TCAM/SRAM初始化镜像
        │  └─ PHV字段分配映射表
        ▼
  硬件配置文件（.hwcfg）
  → 烧录到Parser/MAU/Deparser寄存器
```

数据面C代码在**编译时**完全转化为硬件配置，运行时不消耗CPU资源。

### 1.3.2 控制面：运行时动态管理

```
用户C代码（控制面）
        │
        ▼
  RISC-V GCC 工具链（riscv64-unknown-elf-gcc）
        │
        ▼
  ELF可执行文件
        │
        ▼
  XiangShan RISC-V Core（1.5 GHz）
        │
        ▼
  HAL（Hardware Abstraction Layer）
        │  ├─ hal_tcam_insert() / hal_tcam_delete()
        │  ├─ hal_route_add() / hal_route_del()
        │  ├─ hal_port_config()
        │  └─ hal_meter_set()
        ▼
  TUE（Table Update Engine）
        │  ├─ 原子写入（shadow write + pointer swap）
        │  ├─ 事务队列（64 entry）
        │  └─ 跨时钟域同步
        ▼
  硬件表（TCAM/SRAM/寄存器）
```

控制面C代码编译为标准RISC-V二进制，运行在片上XiangShan核心上，通过HAL API操作硬件表项。

---

# 二、顶层架构

## 2.1 完整系统架构框图

```
╔══════════════════════════════════════════════════════════════════════════════════╗
║                        RV-P4 Switch ASIC 顶层架构                               ║
╠══════════════════════════════════════════════════════════════════════════════════╣
║                                                                                  ║
║  ┌─────────────────────────────────────────────────────────────────────────┐    ║
║  │                        用户编程层（片外）                                │    ║
║  │                                                                         │    ║
║  │  ┌──────────────────────┐      ┌──────────────────────────────────┐    │    ║
║  │  │   数据面 C 代码       │      │      控制面 C 代码                │    │    ║
║  │  │  dataplane.c         │      │   route_mgr.c / acl_mgr.c        │    │    ║
║  │  │  [PARSER] parse_eth()│      │   port_mgr.c / stats.c           │    │    ║
║  │  │  [TABLE]  ipv4_lpm() │      │                                  │    │    ║
║  │  │  [ACTION] fwd()      │      │                                  │    │    ║
║  │  └──────────┬───────────┘      └──────────────┬───────────────────┘    │    ║
║  └─────────────┼──────────────────────────────────┼───────────────────────┘    ║
║                │                                  │                             ║
║                ▼                                  ▼                             ║
║  ┌─────────────────────────┐      ┌───────────────────────────────────────┐    ║
║  │   RV-P4 C 编译器        │      │   RISC-V GCC 工具链                   │    ║
║  │  (LLVM-based Frontend)  │      │   riscv64-unknown-elf-gcc             │    ║
║  │                         │      │                                       │    ║
║  │  ┌─────────────────┐    │      │   ┌───────────────────────────────┐  │    ║
║  │  │ C Frontend      │    │      │   │  标准C编译 → RISC-V ELF       │  │    ║
║  │  │ IR生成          │    │      │   └───────────────────────────────┘  │    ║
║  │  │ 表依赖分析      │    │      │                                       │    ║
║  │  │ PHV分配         │    │      └──────────────────┬────────────────────┘    ║
║  │  │ HW后端代码生成  │    │                         │                         ║
║  │  └────────┬────────┘    │                         │ ELF Binary              ║
║  │           │ .hwcfg      │                         │                         ║
║  └───────────┼─────────────┘                         │                         ║
║              │                                       │                         ║
║              │ 硬件配置文件                           │                         ║
║              │ (Parser FSM表/MAU初始镜像)             │                         ║
║              │                                       │                         ║
╠══════════════╪═══════════════════════════════════════╪═════════════════════════╣
║              │          片上硬件                      │                         ║
║              │                                       ▼                         ║
║              │                        ┌──────────────────────────┐             ║
║              │                        │   XiangShan RISC-V Core  │             ║
║              │                        │   1.5 GHz, RV64GC        │             ║
║              │                        │   L1I$: 32KB             │             ║
║              │                        │   L1D$: 32KB             │             ║
║              │                        │   L2$:  512KB            │             ║
║              │                        └──────────┬───────────────┘             ║
║              │                                   │                             ║
║              │                                   ▼                             ║
║              │                        ┌──────────────────────────┐             ║
║              │                        │   HAL（硬件抽象层）       │             ║
║              │                        │   hal_tcam_insert()      │             ║
║              │                        │   hal_route_add()        │             ║
║              │                        │   hal_port_config()      │             ║
║              │                        └──────────┬───────────────┘             ║
║              │                                   │                             ║
║              │                                   ▼                             ║
║              │                        ┌──────────────────────────┐             ║
║              │                        │   TUE（表更新引擎）       │             ║
║              │                        │   原子写入/事务队列       │             ║
║              │                        │   APB寄存器接口          │             ║
║              │                        └──────────┬───────────────┘             ║
║              │                                   │                             ║
║              ▼                                   ▼                             ║
║  ┌───────────────────────────────────────────────────────────────────────┐     ║
║  │                    数据面硬件流水线（clk_dp = 1 GHz）                  │     ║
║  │                                                                       │     ║
║  │  ┌──────────┐   ┌──────────────────────────────────────┐  ┌───────┐  │     ║
║  │  │          │   │         MAU × 24 Stages               │  │       │  │     ║
║  │  │  Parser  │   │  ┌──────┐ ┌──────┐     ┌──────┐      │  │  TM   │  │     ║
║  │  │          ├──►│  │ S0   │ │ S1   │ ... │ S23  │      ├─►│       │  │     ║
║  │  │  FSM     │   │  │TCAM  │ │TCAM  │     │TCAM  │      │  │ 256Q  │  │     ║
║  │  │  64状态  │   │  │SRAM  │ │SRAM  │     │SRAM  │      │  │ DWRR  │  │     ║
║  │  │  PHV生成 │   │  │ALU   │ │ALU   │     │ALU   │      │  │ SP    │  │     ║
║  │  │          │   │  └──────┘ └──────┘     └──────┘      │  │       │  │     ║
║  │  └──────────┘   └──────────────────────────────────────┘  └───┬───┘  │     ║
║  │       ▲                                                        │      │     ║
║  │       │ 入包                                                   ▼      │     ║
║  │  ┌────┴─────┐                                           ┌──────────┐  │     ║
║  │  │ 32×400G  │                                           │ Deparser │  │     ║
║  │  │ SerDes   │◄──────────────────────────────────────────│ PHV序列化│  │     ║
║  │  │ MAC/PCS  │          出包                             │ 校验和   │  │     ║
║  │  └──────────┘                                           └──────────┘  │     ║
║  └───────────────────────────────────────────────────────────────────────┘     ║
╚══════════════════════════════════════════════════════════════════════════════════╝
```

## 2.2 模块层次树

```
rv_p4_top
├── clk_rst_gen                    # 时钟/复位生成模块
│   ├── pll_dp                     # 数据面PLL（1 GHz）
│   ├── pll_ctrl                   # 控制面PLL（1.5 GHz）
│   └── rst_sync                   # 复位同步器
│
├── cpu_subsystem                  # 控制面子系统
│   ├── xiangshan_core             # RISC-V XiangShan处理器
│   │   ├── frontend               # 取指/译码
│   │   ├── backend                # 执行/提交
│   │   ├── l1i_cache              # 32KB L1指令缓存
│   │   ├── l1d_cache              # 32KB L1数据缓存
│   │   └── l2_cache               # 512KB L2缓存
│   ├── plic                       # 平台级中断控制器
│   ├── clint                      # 核心本地中断器
│   ├── axi_interconnect           # AXI总线互联
│   └── ddr_ctrl                   # DDR4控制器（片外DRAM）
│
├── tue                            # 表更新引擎
│   ├── apb_slave                  # APB从接口
│   ├── txn_queue                  # 事务队列（64 entry）
│   ├── shadow_buf                 # 影子缓冲区
│   ├── ptr_swap                   # 指针交换逻辑
│   └── cdc_sync                   # 跨时钟域同步（ctrl→dp）
│
├── parser                         # 报文解析器
│   ├── pkt_buf                    # 入包缓冲（4KB）
│   ├── fsm_ctrl                   # FSM控制器（64状态）
│   ├── tcam_parse                 # 解析TCAM（64×640b）
│   ├── phv_builder                # PHV构建器
│   └── phv_fifo                   # PHV输出FIFO
│
├── mau_pipeline                   # MAU流水线
│   ├── mau_stage[0..23]           # 24个MAU Stage
│   │   ├── phv_crossbar           # PHV交叉开关
│   │   ├── tcam_array             # TCAM阵列（2K×512b）
│   │   ├── action_sram            # Action SRAM（64K×128b）
│   │   ├── stateful_sram          # 有状态SRAM（16K×64b）
│   │   ├── hash_unit              # Hash单元（CRC32/16/Jenkins）
│   │   └── alu                    # 算术逻辑单元
│   └── phv_bus                    # Stage间PHV总线
│
├── traffic_manager                # 流量管理器
│   ├── ingress_buf                # 入口缓冲
│   ├── queue_mgr                  # 队列管理（256队列）
│   ├── scheduler                  # 调度器（DWRR+SP）
│   ├── mcast_engine               # 组播复制引擎
│   ├── wred_engine                # WRED拥塞控制
│   └── egress_buf                 # 出口缓冲
│
├── deparser                       # 报文重组器
│   ├── phv_deserializer           # PHV反序列化
│   ├── csum_engine                # 校验和计算引擎
│   └── pkt_assembler              # 报文组装
│
└── mac_pcs_serdes                 # 物理层接口
    ├── mac[0..31]                 # 32个MAC（400GbE）
    ├── pcs[0..31]                 # PCS层
    └── serdes[0..31]              # SerDes（112G PAM4）

```

---

# 三、数据面硬件模块设计

## 3.1 Parser

### 3.1.1 功能概述

Parser 负责从入口报文中提取协议头字段，填充 PHV（Packet Header Vector），并将 PHV 传递给 MAU 流水线。Parser 采用有限状态机（FSM）实现，支持最多 64 个解析状态，每个状态通过 TCAM 查找确定下一状态和字段提取规则。

### 3.1.2 FSM 设计

#### 状态编码

| 状态ID | 状态名 | 说明 |
|--------|--------|------|
| 0x00 | START | 初始状态，从以太网头开始 |
| 0x01 | PARSE_ETH | 解析以太网头（14B） |
| 0x02 | PARSE_VLAN | 解析802.1Q VLAN标签（4B） |
| 0x03 | PARSE_QINQ | 解析QinQ双层VLAN（8B） |
| 0x04 | PARSE_IPV4 | 解析IPv4头（20B基础） |
| 0x05 | PARSE_IPV4_OPT | 解析IPv4选项 |
| 0x06 | PARSE_IPV6 | 解析IPv6头（40B） |
| 0x07 | PARSE_IPV6_EXT | 解析IPv6扩展头 |
| 0x08 | PARSE_TCP | 解析TCP头（20B基础） |
| 0x09 | PARSE_UDP | 解析UDP头（8B） |
| 0x0A | PARSE_ICMP | 解析ICMP头（8B） |
| 0x0B | PARSE_MPLS | 解析MPLS标签栈 |
| 0x0C | PARSE_VXLAN | 解析VXLAN头（8B） |
| 0x0D | PARSE_GRE | 解析GRE头 |
| 0x0E | PARSE_GENEVE | 解析Geneve头 |
| 0x0F | PARSE_INT | 解析INT遥测头 |
| 0x10-0x3E | 用户自定义 | 用户C代码定义的协议状态 |
| 0x3F | ACCEPT | 解析完成，输出PHV |

#### TCAM Entry 位域定义（640b = 80B）

每条 Parser TCAM entry 为 640 位，格式如下：

```
 位域范围        宽度    字段名              说明
 ─────────────────────────────────────────────────────────────────
 [639:576]       64b    current_state       当前FSM状态（one-hot或二进制编码）
 [575:512]       64b    current_state_mask  状态掩码（TCAM掩码位）
 [511:384]      128b    lookahead_data      前瞻数据（当前偏移处的128b报文数据）
 [383:256]      128b    lookahead_mask      前瞻数据掩码
 [255:192]       64b    next_state          匹配后跳转的下一状态
 [191:128]       64b    extract_offset      字段提取起始偏移（单位：bit）
 [127:64]        64b    extract_length      字段提取长度（单位：bit）
 [63:48]         16b    phv_dest_field      目标PHV字段ID
 [47:32]         16b    advance_bytes       解析指针前进字节数
 [31:16]         16b    flags               控制标志（见下表）
 [15:0]          16b    priority            优先级（数值越小优先级越高）
```

flags 字段位域定义：

```
 位    字段名              说明
 ─────────────────────────────────────────────────────────────────
 [15]  valid               entry有效位
 [14]  last_extract        最后一次提取（本状态提取完成）
 [13]  set_accept          置位后直接跳转ACCEPT状态
 [12]  loop_enable         允许循环解析（如MPLS标签栈）
 [11]  loop_max[3:0]       最大循环次数（4b，最多15次）
 [7]   checksum_en         使能校验和验证
 [6]   timestamp_en        使能时间戳提取
 [5]   metadata_set        设置元数据字段
 [4:0] reserved            保留
```

#### FSM 状态转移示意

```
                    ┌─────────┐
                    │  START  │
                    └────┬────┘
                         │ 任意报文
                         ▼
                    ┌─────────┐
                    │PARSE_ETH│ 提取: dst_mac, src_mac, ethertype
                    └────┬────┘
              ┌──────────┼──────────┬──────────┐
         0x8100│     0x88A8│    0x0800│    0x86DD│
              ▼           ▼          ▼           ▼
         ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
         │PARSE_   │ │PARSE_   │ │PARSE_IPV4│ │PARSE_IPV6│
         │VLAN     │ │QINQ     │ └────┬─────┘ └────┬─────┘
         └────┬────┘ └────┬────┘      │              │
              │           │      ┌────┴────┐    ┌────┴────┐
              └─────┬─────┘      │proto=6  │    │NH=6/17  │
                    │            │TCP      │    │TCP/UDP  │
                    │            ├─────────┤    └─────────┘
                    │            │proto=17 │
                    │            │UDP      │
                    │            ├─────────┤
                    │            │proto=1  │
                    │            │ICMP     │
                    │            └────┬────┘
                    └─────────────────┘
                                      │
                                      ▼
                                 ┌─────────┐
                                 │ ACCEPT  │ → 输出PHV到MAU
                                 └─────────┘
```

### 3.1.3 PHV 结构（512B）

PHV（Packet Header Vector）是贯穿整个数据面流水线的核心数据结构，总大小 512 字节。

#### PHV 字段布局表

```
 偏移(B)  大小(B)  字段名                  说明
 ──────────────────────────────────────────────────────────────────
 0        6        eth.dst_mac             目的MAC地址
 6        6        eth.src_mac             源MAC地址
 12       2        eth.ethertype           以太网类型
 14       2        vlan.tci                VLAN TCI（含PCP/DEI/VID）
 16       2        vlan.ethertype          内层以太网类型
 18       2        qinq.outer_tci          QinQ外层TCI
 20       2        qinq.inner_tci          QinQ内层TCI
 22       2        qinq.ethertype          QinQ内层以太网类型
 24       1        ipv4.version_ihl        版本+IHL
 25       1        ipv4.dscp_ecn           DSCP+ECN
 26       2        ipv4.total_len          总长度
 28       2        ipv4.id                 标识
 30       2        ipv4.flags_frag_off     标志+分片偏移
 32       1        ipv4.ttl                生存时间
 33       1        ipv4.protocol           上层协议
 34       2        ipv4.checksum           头部校验和
 36       4        ipv4.src_addr           源IP地址
 40       4        ipv4.dst_addr           目的IP地址
 44       16       ipv6.src_addr           IPv6源地址
 60       16       ipv6.dst_addr           IPv6目的地址
 76       2        ipv6.payload_len        IPv6载荷长度
 78       1        ipv6.next_hdr           下一头部
 79       1        ipv6.hop_limit          跳数限制
 80       1        ipv6.traffic_class      流量类别
 81       3        ipv6.flow_label         流标签
 84       2        tcp.src_port            TCP源端口
 86       2        tcp.dst_port            TCP目的端口
 88       4        tcp.seq_num             序列号
 92       4        tcp.ack_num             确认号
 96       1        tcp.data_offset_flags   数据偏移+标志
 97       1        tcp.flags               TCP标志（SYN/ACK/FIN等）
 98       2        tcp.window              窗口大小
 100      2        tcp.checksum            TCP校验和
 102      2        tcp.urgent_ptr          紧急指针
 104      2        udp.src_port            UDP源端口
 106      2        udp.dst_port            UDP目的端口
 108      2        udp.length              UDP长度
 110      2        udp.checksum            UDP校验和
 112      4        icmp.type_code_csum     ICMP类型+代码+校验和
 116      4        icmp.rest               ICMP剩余头部
 120      4        mpls[0].label_exp_s_ttl MPLS标签0
 124      4        mpls[1].label_exp_s_ttl MPLS标签1
 128      4        mpls[2].label_exp_s_ttl MPLS标签2
 132      4        mpls[3].label_exp_s_ttl MPLS标签3
 136      8        vxlan.flags_reserved_vni VXLAN头
 144      8        gre.header              GRE头
 152      8        geneve.header           Geneve头
 160      64       user_defined[0..15]     用户自定义字段（16×4B）
 224      32       int_header              INT遥测头（最大32B）
 256      8        meta.ingress_port       入口端口号（16b有效）
 258      8        meta.egress_port        出口端口号（16b有效）
 260      4        meta.ingress_timestamp  入口时间戳（ns，32b）
 264      4        meta.pkt_len            报文长度（字节）
 268      2        meta.pkt_type           报文类型（单播/组播/广播）
 270      2        meta.drop_reason        丢包原因码
 272      4        meta.hash_value         流Hash值
 276      4        meta.color              Meter颜色（绿/黄/红）
 280      4        meta.qos_class          QoS类别
 284      4        meta.vrf_id             VRF标识
 288      4        meta.tunnel_id          隧道标识
 292      4        meta.nexthop_id         下一跳标识
 296      8        meta.cookie             用户自定义Cookie（64b）
 304      16       meta.scratch[0..3]      暂存寄存器（4×4B）
 320      192      reserved                保留（对齐到512B）
 ──────────────────────────────────────────────────────────────────
 总计     512B
```

### 3.1.4 解析时序（Cycle-by-Cycle）

Parser 工作在 clk_dp（1 GHz），每个报文的解析流程如下：

```
Cycle  操作
─────────────────────────────────────────────────────────────────
  0    入包第一个cell（64B）到达pkt_buf，触发解析启动
       FSM进入START状态，加载前128b数据到lookahead寄存器
  1    TCAM查找：以(current_state, lookahead[127:0])为key
       查找Parser TCAM（64条目），获得匹配entry
  2    TCAM结果就绪：next_state, extract_offset, extract_length,
       phv_dest_field, advance_bytes
  3    字段提取：从pkt_buf按offset/length提取字段，写入PHV
       FSM跳转到next_state，解析指针前进advance_bytes
  4    若next_state != ACCEPT，重复Cycle 1-3
       典型以太网/IPv4/TCP报文需要4次迭代（4个状态）
  N    FSM到达ACCEPT状态，PHV构建完成
       PHV写入phv_fifo，等待MAU流水线消费
  N+1  PHV从phv_fifo出队，进入MAU Stage 0
```

典型协议解析延迟：

| 协议栈 | 状态数 | 解析周期数 |
|--------|--------|-----------|
| ETH only | 2 | 4 |
| ETH + IPv4 | 3 | 6 |
| ETH + IPv4 + TCP | 4 | 8 |
| ETH + VLAN + IPv4 + UDP | 5 | 10 |
| ETH + QinQ + IPv4 + TCP | 6 | 12 |
| ETH + MPLS(3) + IPv4 + UDP | 7 | 14 |
| ETH + IPv4 + GRE + IPv4 + TCP | 6 | 12 |

### 3.1.5 Parser 接口信号表

| 信号名 | 方向 | 宽度 | 时钟域 | 说明 |
|--------|------|------|--------|------|
| clk_dp | input | 1 | - | 数据面时钟（1 GHz） |
| rst_dp_n | input | 1 | - | 数据面复位（低有效） |
| pkt_data_in | input | 512 | clk_dp | 入包数据（64B/cycle） |
| pkt_valid_in | input | 1 | clk_dp | 入包数据有效 |
| pkt_sop_in | input | 1 | clk_dp | 报文起始标志 |
| pkt_eop_in | input | 1 | clk_dp | 报文结束标志 |
| pkt_len_in | input | 14 | clk_dp | 报文总长度（字节） |
| pkt_port_in | input | 6 | clk_dp | 入口端口号（0-31） |
| pkt_ready_out | output | 1 | clk_dp | 背压信号（低时停止发送） |
| phv_data_out | output | 4096 | clk_dp | PHV输出（512B） |
| phv_valid_out | output | 1 | clk_dp | PHV有效 |
| phv_ready_in | input | 1 | clk_dp | MAU就绪信号 |
| tcam_cfg_addr | input | 6 | clk_ctrl | TCAM配置地址（0-63） |
| tcam_cfg_data | input | 640 | clk_ctrl | TCAM配置数据 |
| tcam_cfg_we | input | 1 | clk_ctrl | TCAM写使能 |
| parse_err_out | output | 8 | clk_dp | 解析错误码 |
| pkt_drop_out | output | 1 | clk_dp | 丢包指示 |

---

## 3.2 MAU Stage（×24）

### 3.2.1 功能概述

MAU（Match-Action Unit）是数据面流水线的核心处理单元，共 24 个 Stage 串联。每个 Stage 接收上游 PHV，执行 TCAM 匹配，根据匹配结果从 Action SRAM 读取动作，由 ALU 执行动作修改 PHV，然后将修改后的 PHV 传递给下一 Stage。

### 3.2.2 内部 4 子级流水

每个 MAU Stage 内部分为 4 个子级流水，总延迟 6 cycles：

```
  PHV输入
    │
    ▼  Cycle 0
  ┌─────────────────────────────────────────────────────┐
  │  子级1: PHV Crossbar（交叉开关）                     │
  │  • 从512B PHV中选取最多64B关键字段                   │
  │  • 可配置字段选择（编译时确定）                       │
  │  • 输出：match_key（最大512b）                       │
  └──────────────────────┬──────────────────────────────┘
                         │
                         ▼  Cycle 1-2
  ┌─────────────────────────────────────────────────────┐
  │  子级2: TCAM 查找                                    │
  │  • 2K×512b TCAM阵列                                 │
  │  • 优先级编码器（最高优先级匹配）                     │
  │  • 输出：action_addr（16b，指向Action SRAM）         │
  │  • 输出：hit/miss标志                                │
  └──────────────────────┬──────────────────────────────┘
                         │
                         ▼  Cycle 3
  ┌─────────────────────────────────────────────────────┐
  │  子级3: Action SRAM 读取                             │
  │  • 64K×128b Action SRAM                             │
  │  • 以action_addr为索引读取128b动作编码               │
  │  • 输出：action_word（128b）                         │
  └──────────────────────┬──────────────────────────────┘
                         │
                         ▼  Cycle 4-5
  ┌─────────────────────────────────────────────────────┐
  │  子级4: ALU 执行                                     │
  │  • 解码action_word，执行PHV字段修改                  │
  │  • 支持：SET/ADD/SUB/AND/OR/XOR/COPY/DROP/METER     │
  │  • 有状态操作：READ/WRITE/ADD/CAS（访问Stateful SRAM）│
  │  • 输出：修改后的PHV                                 │
  └──────────────────────┬──────────────────────────────┘
                         │
                         ▼
                      PHV输出（到下一Stage或TM）
```

### 3.2.3 TCAM 规格

| 参数 | 值 | 说明 |
|------|-----|------|
| 条目数 | 2048 | 每Stage 2K条目 |
| 条目宽度 | 512b | 匹配键宽度 |
| 存储类型 | 三值CAM（0/1/X） | X表示don't care |
| 优先级 | 按行号，行0最高 | 硬件优先级编码器 |
| 查找延迟 | 2 cycles | 流水化实现 |
| 更新方式 | 通过TUE原子写入 | 不影响在途报文 |
| 功耗模式 | 分段使能 | 未使用段可关闭 |
| ECC保护 | SECDED | 单bit纠错，双bit检错 |

TCAM 条目格式（512b）：

```
 位域范围      宽度    字段名          说明
 ──────────────────────────────────────────────────────
 [511:448]     64b    key_data[0]     匹配键数据第0段
 [447:384]     64b    key_data[1]     匹配键数据第1段
 [383:320]     64b    key_data[2]     匹配键数据第2段
 [319:256]     64b    key_data[3]     匹配键数据第3段
 [255:192]     64b    key_data[4]     匹配键数据第4段
 [191:128]     64b    key_data[5]     匹配键数据第5段
 [127:64]      64b    key_data[6]     匹配键数据第6段
 [63:0]        64b    key_data[7]     匹配键数据第7段
 （掩码存储在独立的MASK阵列，与DATA阵列一一对应）
```

### 3.2.4 Action SRAM 规格

| 参数 | 值 | 说明 |
|------|-----|------|
| 条目数 | 65536 | 每Stage 64K条目 |
| 条目宽度 | 128b | 动作编码宽度 |
| 存储类型 | SRAM | 标准静态RAM |
| 读延迟 | 1 cycle | 单周期读取 |
| 写延迟 | 1 cycle | 通过TUE写入 |
| ECC保护 | SECDED | 单bit纠错 |
| 总容量 | 1 MB/Stage | 24 Stage共24 MB |

Action Word 编码格式（128b）：

```
 位域范围      宽度    字段名          说明
 ──────────────────────────────────────────────────────
 [127:120]     8b     opcode          动作操作码（见下表）
 [119:112]     8b     dst_field       目标PHV字段ID
 [111:96]      16b    src_field_a     源字段A（PHV字段ID或立即数索引）
 [95:80]       16b    src_field_b     源字段B
 [79:64]       16b    imm_value       立即数值（16b）
 [63:48]       16b    meter_id        Meter ID（用于METER操作）
 [47:32]       16b    counter_id      计数器ID
 [31:16]       16b    next_table      下一张表ID（表链）
 [15:8]        8b     flags           动作标志
 [7:0]         8b     reserved        保留
```

操作码（opcode）定义：

| opcode | 助记符 | 操作 | 说明 |
|--------|--------|------|------|
| 0x00 | NOP | 无操作 | 透传PHV |
| 0x01 | SET | dst = imm | 设置字段为立即数 |
| 0x02 | COPY | dst = src_a | 字段复制 |
| 0x03 | ADD | dst = src_a + src_b | 加法 |
| 0x04 | SUB | dst = src_a - src_b | 减法 |
| 0x05 | AND | dst = src_a & src_b | 按位与 |
| 0x06 | OR | dst = src_a \| src_b | 按位或 |
| 0x07 | XOR | dst = src_a ^ src_b | 按位异或 |
| 0x08 | SHL | dst = src_a << imm | 左移 |
| 0x09 | SHR | dst = src_a >> imm | 右移 |
| 0x0A | DROP | 丢弃报文 | 设置drop标志 |
| 0x0B | METER | 执行Meter操作 | 更新令牌桶，返回颜色 |
| 0x0C | COUNTER | 递增计数器 | 原子加1 |
| 0x0D | HASH | dst = hash(src_a) | Hash计算 |
| 0x0E | REDIRECT | 修改出口端口 | 设置egress_port |
| 0x0F | ENCAP | 添加隧道头 | 触发Deparser封装 |
| 0x10 | DECAP | 剥离隧道头 | 触发Deparser解封装 |
| 0x11 | MIRROR | 镜像报文 | 复制到镜像端口 |
| 0x12-0xFF | 保留 | - | 未来扩展 |

### 3.2.5 Stateful SRAM 操作

每个 MAU Stage 包含一块 Stateful SRAM（16K×64b），用于存储有状态数据（计数器、Meter令牌桶、流表状态等）。

| 参数 | 值 |
|------|-----|
| 容量 | 16K × 64b |
| 操作类型 | READ / WRITE / ADD / CAS |
| 原子性 | 硬件保证单周期原子操作 |
| 并发访问 | 每cycle最多1次读写 |

支持的原子操作：

```c
// READ：读取64b值
uint64_t stateful_read(uint16_t addr);

// WRITE：写入64b值
void stateful_write(uint16_t addr, uint64_t value);

// ADD：原子加法（用于计数器/字节计数）
uint64_t stateful_add(uint16_t addr, uint64_t delta);

// CAS：比较并交换（用于状态机转换）
// 若 mem[addr] == expected，则写入new_val，返回true
// 否则返回false，mem[addr]不变
bool stateful_cas(uint16_t addr, uint64_t expected, uint64_t new_val);
```

### 3.2.6 Hash 单元

每个 MAU Stage 内置 Hash 单元，支持以下算法：

| 算法 | 输入宽度 | 输出宽度 | 用途 |
|------|----------|----------|------|
| CRC32 | 最大512b | 32b | 流Hash、ECMP选路 |
| CRC16 | 最大256b | 16b | 快速Hash |
| Jenkins | 最大512b | 32b | 哈希表索引 |
| Identity | 任意 | 原样输出 | 直接索引 |

Hash 输入字段由编译器在编译时配置，通过 PHV Crossbar 选取。

### 3.2.7 MAU Stage 接口信号表

| 信号名 | 方向 | 宽度 | 时钟域 | 说明 |
|--------|------|------|--------|------|
| clk_dp | input | 1 | - | 数据面时钟 |
| rst_dp_n | input | 1 | - | 复位 |
| phv_in | input | 4096 | clk_dp | 输入PHV（512B） |
| phv_valid_in | input | 1 | clk_dp | PHV有效 |
| phv_ready_out | output | 1 | clk_dp | 背压 |
| phv_out | output | 4096 | clk_dp | 输出PHV |
| phv_valid_out | output | 1 | clk_dp | 输出有效 |
| phv_ready_in | input | 1 | clk_dp | 下游就绪 |
| tcam_cfg_addr | input | 11 | clk_ctrl | TCAM配置地址（0-2047） |
| tcam_cfg_data | input | 512 | clk_ctrl | TCAM数据 |
| tcam_cfg_mask | input | 512 | clk_ctrl | TCAM掩码 |
| tcam_cfg_we | input | 1 | clk_ctrl | TCAM写使能 |
| asram_cfg_addr | input | 16 | clk_ctrl | Action SRAM地址 |
| asram_cfg_data | input | 128 | clk_ctrl | Action SRAM数据 |
| asram_cfg_we | input | 1 | clk_ctrl | Action SRAM写使能 |
| ssram_addr | input | 14 | clk_dp | Stateful SRAM地址 |
| ssram_rdata | output | 64 | clk_dp | 读数据 |
| ssram_wdata | input | 64 | clk_dp | 写数据 |
| ssram_op | input | 2 | clk_dp | 操作类型（00=R,01=W,10=ADD,11=CAS） |
| stage_id | input | 5 | - | Stage编号（0-23），静态配置 |
| drop_out | output | 1 | clk_dp | 丢包指示 |
| err_out | output | 8 | clk_dp | 错误码 |

---

## 3.3 Traffic Manager

### 3.3.1 功能概述

Traffic Manager（TM）负责报文的缓存、调度、组播复制和拥塞控制。TM 位于 MAU 流水线之后、Deparser 之前，是数据面的流量控制核心。

### 3.3.2 队列结构

| 参数 | 值 | 说明 |
|------|-----|------|
| 总队列数 | 256 | 32端口 × 8队列/端口 |
| 队列深度 | 动态共享 | 共享缓冲池 + 保证最小深度 |
| 共享缓冲池 | 64 MB | 片外DDR4，通过AXI访问 |
| 每队列保证深度 | 64 KB | 片上SRAM保证 |
| 队列类型 | 8个优先级 | Q0最高（SP），Q1-Q7 DWRR |
| 最大帧长 | 9600B | 支持Jumbo Frame |

队列映射关系：

```
端口0:  Q0(SP) Q1 Q2 Q3 Q4 Q5 Q6 Q7(DWRR)
端口1:  Q0(SP) Q1 Q2 Q3 Q4 Q5 Q6 Q7(DWRR)
...
端口31: Q0(SP) Q1 Q2 Q3 Q4 Q5 Q6 Q7(DWRR)

队列ID = port_id × 8 + priority
```

### 3.3.3 调度算法

TM 采用两级层次化调度：

```
第一级：端口内调度（每端口8队列）
  ├── Q0: Strict Priority（最高优先级，用于控制报文）
  └── Q1-Q7: DWRR（Deficit Weighted Round Robin）
              权重可配置（每队列8b权重，0-255）

第二级：端口间调度
  └── 按端口带宽配置进行调度
      支持端口级速率限制（令牌桶）
```

DWRR 参数：

| 参数 | 范围 | 默认值 | 说明 |
|------|------|--------|------|
| 队列权重 | 1-255 | 1 | 相对带宽权重 |
| Quantum | 1500-9600B | 1500B | 每轮最大发送量 |
| Deficit计数器 | 32b | 0 | 累积亏空字节数 |

### 3.3.4 组播复制

| 参数 | 值 | 说明 |
|------|-----|------|
| 组播组数 | 4096 | 组ID 0-4095 |
| 每组最大成员数 | 64 | 端口+队列对 |
| 复制方式 | Copy-on-Write | 共享报文体，独立头部 |
| 复制延迟 | 1 cycle/成员 | 流水化复制 |
| 组播表存储 | 片上SRAM | 4K×64×(6+3)b = 288 KB |

组播表结构：

```c
typedef struct {
    uint16_t  group_id;          // 组播组ID（0-4095）
    uint8_t   member_count;      // 成员数量（1-64）
    struct {
        uint8_t  port_id;        // 出口端口（0-31）
        uint8_t  queue_id;       // 出口队列（0-7）
        uint8_t  rid;            // 复制ID（用于去重）
    } members[64];
} mcast_group_t;
```

### 3.3.5 WRED 参数

每个队列独立配置 WRED（Weighted Random Early Detection）：

| 参数 | 范围 | 说明 |
|------|------|------|
| min_threshold | 0-64KB | 开始丢包的队列深度 |
| max_threshold | 0-64KB | 100%丢包的队列深度 |
| max_drop_prob | 0-100% | 最大丢包概率 |
| weight | 1-16 | 平均队列深度计算权重 |

WRED 丢包概率计算：

```
若 avg_depth < min_threshold：丢包概率 = 0
若 avg_depth > max_threshold：丢包概率 = 100%
否则：丢包概率 = max_drop_prob × (avg_depth - min_threshold)
                              / (max_threshold - min_threshold)
```

### 3.3.6 TM 接口信号表

| 信号名 | 方向 | 宽度 | 时钟域 | 说明 |
|--------|------|------|--------|------|
| clk_dp | input | 1 | - | 数据面时钟 |
| rst_dp_n | input | 1 | - | 复位 |
| phv_in | input | 4096 | clk_dp | 来自MAU的PHV |
| phv_valid_in | input | 1 | clk_dp | PHV有效 |
| phv_ready_out | output | 1 | clk_dp | 背压 |
| pkt_data_in | input | 512 | clk_dp | 报文数据（与PHV对应） |
| pkt_valid_in | input | 1 | clk_dp | 报文数据有效 |
| pkt_sop_in | input | 1 | clk_dp | 报文起始 |
| pkt_eop_in | input | 1 | clk_dp | 报文结束 |
| phv_out | output | 4096 | clk_dp | 到Deparser的PHV |
| phv_valid_out | output | 1 | clk_dp | PHV有效 |
| phv_ready_in | input | 1 | clk_dp | Deparser就绪 |
| pkt_data_out | output | 512 | clk_dp | 报文数据输出 |
| pkt_valid_out | output | 1 | clk_dp | 报文数据有效 |
| pkt_sop_out | output | 1 | clk_dp | 报文起始 |
| pkt_eop_out | output | 1 | clk_dp | 报文结束 |
| port_id_out | output | 6 | clk_dp | 出口端口号 |
| queue_id_out | output | 3 | clk_dp | 出口队列号 |
| tm_cfg_addr | input | 16 | clk_ctrl | TM配置地址 |
| tm_cfg_data | input | 32 | clk_ctrl | TM配置数据 |
| tm_cfg_we | input | 1 | clk_ctrl | TM配置写使能 |
| mcast_grp_addr | input | 12 | clk_ctrl | 组播表地址 |
| mcast_grp_data | input | 512 | clk_ctrl | 组播表数据 |
| mcast_grp_we | input | 1 | clk_ctrl | 组播表写使能 |
| drop_cnt_out | output | 32 | clk_dp | 丢包计数 |
| queue_depth_out | output | 32 | clk_dp | 当前队列深度 |
| axi_m_* | output | - | clk_dp | AXI主接口（访问DDR4） |

---

## 3.4 Deparser

### 3.4.1 功能概述

Deparser 接收来自 TM 的 PHV 和原始报文数据，根据 PHV 中的字段值重新序列化报文头部，并将修改后的报文发送到出口 MAC。

### 3.4.2 PHV 序列化流程

```
Cycle  操作
─────────────────────────────────────────────────────────────────
  0    接收PHV和报文数据，读取meta.egress_port确定出口
       读取PHV中的有效字段标志（valid_flags）
  1    根据valid_flags确定需要序列化的协议头列表
       计算新报文头总长度
  2    按协议栈顺序从PHV提取字段，构建新报文头
       ETH → VLAN（可选）→ IP → TCP/UDP/ICMP
  3    若有隧道封装（ENCAP动作），在外层添加隧道头
       若有隧道解封装（DECAP动作），剥离外层头部
  4    触发校验和增量更新引擎
  5    将新报文头与原始报文载荷拼接
       输出到出口MAC FIFO
```

### 3.4.3 校验和增量更新

Deparser 支持 IP/TCP/UDP 校验和的增量更新，避免重新计算整个报文的校验和。

#### IP 校验和增量更新

基于 RFC 1624 的增量更新算法：

```
新校验和 = ~(~旧校验和 + ~旧字段值 + 新字段值)

其中：
  旧校验和 = PHV中读取的原始校验和
  旧字段值 = 被修改字段的原始值
  新字段值 = 修改后的值
  ~ 表示按位取反
  + 表示一补数加法（进位回卷）
```

支持的增量更新场景：

| 场景 | 修改字段 | 更新校验和 |
|------|----------|-----------|
| TTL递减 | ipv4.ttl -= 1 | ipv4.checksum |
| 源IP修改（NAT） | ipv4.src_addr | ipv4.checksum + tcp/udp.checksum |
| 目的IP修改（NAT） | ipv4.dst_addr | ipv4.checksum + tcp/udp.checksum |
| 源端口修改（NAPT） | tcp/udp.src_port | tcp/udp.checksum |
| 目的端口修改（NAPT） | tcp/udp.dst_port | tcp/udp.checksum |
| DSCP修改（QoS） | ipv4.dscp_ecn | ipv4.checksum |

### 3.4.4 Deparser 接口信号表

| 信号名 | 方向 | 宽度 | 时钟域 | 说明 |
|--------|------|------|--------|------|
| clk_dp | input | 1 | - | 数据面时钟 |
| rst_dp_n | input | 1 | - | 复位 |
| phv_in | input | 4096 | clk_dp | 输入PHV（512B） |
| phv_valid_in | input | 1 | clk_dp | PHV有效 |
| phv_ready_out | output | 1 | clk_dp | 背压 |
| pkt_data_in | input | 512 | clk_dp | 原始报文数据 |
| pkt_valid_in | input | 1 | clk_dp | 报文数据有效 |
| pkt_sop_in | input | 1 | clk_dp | 报文起始 |
| pkt_eop_in | input | 1 | clk_dp | 报文结束 |
| pkt_data_out | output | 512 | clk_dp | 重组后报文数据 |
| pkt_valid_out | output | 1 | clk_dp | 输出有效 |
| pkt_sop_out | output | 1 | clk_dp | 报文起始 |
| pkt_eop_out | output | 1 | clk_dp | 报文结束 |
| pkt_port_out | output | 6 | clk_dp | 出口端口号 |
| pkt_len_out | output | 14 | clk_dp | 报文长度 |
| mac_ready_in | input | 1 | clk_dp | MAC就绪 |
| csum_err_out | output | 1 | clk_dp | 校验和错误 |
| deparse_err_out | output | 8 | clk_dp | 解析错误码 |

---

# 四、Table Update Engine（TUE）

## 4.1 功能概述

TUE（Table Update Engine）是控制面与数据面之间的硬件桥梁，负责将控制面（RISC-V CPU）的表项更新请求安全、原子地写入数据面硬件表（TCAM/SRAM）。TUE 解决了跨时钟域写入和在途报文一致性两大核心问题。

## 4.2 原子更新机制

TUE 采用 **Shadow Write + Pointer Swap** 机制保证表项更新的原子性：

```
步骤1: Shadow Write（影子写入）
  ┌─────────────────────────────────────────────────────┐
  │  控制面写入影子缓冲区（Shadow Buffer）               │
  │  Shadow Buffer 与数据面当前使用的表项相互独立        │
  │  写入期间数据面继续使用旧表项，不受影响              │
  └─────────────────────────────────────────────────────┘

步骤2: Pointer Swap（指针交换）
  ┌─────────────────────────────────────────────────────┐
  │  等待当前所有在途报文完成处理（drain等待）           │
  │  原子地将活跃指针从旧表项切换到新表项               │
  │  切换操作在单个时钟周期内完成                        │
  └─────────────────────────────────────────────────────┘

步骤3: 旧表项回收
  ┌─────────────────────────────────────────────────────┐
  │  确认所有报文已使用新表项后                          │
  │  旧表项空间标记为可用，供下次更新使用                │
  └─────────────────────────────────────────────────────┘
```

时序示意：

```
时间轴 ──────────────────────────────────────────────────────►

数据面:  [使用表项A] [使用表项A] [使用表项A] [使用表项B] [使用表项B]
                                              ▲
控制面:              [写Shadow B] [写Shadow B] │ Swap
                                              │
TUE:                 [接收请求]  [写影子区]   [执行Swap] [确认完成]
```

## 4.3 事务队列

TUE 内置 64 entry 的事务队列，支持批量表项更新：

| 参数 | 值 | 说明 |
|------|-----|------|
| 队列深度 | 64 entry | 最多64个待处理事务 |
| 每entry大小 | 128B | 含操作类型+地址+数据 |
| 处理速率 | 1 entry/cycle | @clk_ctrl = 1.5 GHz |
| 最大吞吐 | 1.5M ops/s | 理论峰值 |
| 优先级 | FIFO | 先进先出 |
| 溢出处理 | 背压CPU | 队列满时阻塞APB写 |

事务队列 Entry 格式：

```c
typedef struct {
    uint8_t  op_type;      // 操作类型（见下表）
    uint8_t  stage_id;     // 目标Stage（0-23）
    uint8_t  table_type;   // 表类型（TCAM/ASRAM/SSRAM/TM/PARSER）
    uint8_t  flags;        // 标志（ATOMIC/BATCH_END等）
    uint32_t addr;         // 目标地址
    uint8_t  data[64];     // 写入数据（最大64B）
    uint8_t  mask[64];     // 掩码（TCAM操作时有效）
} txn_entry_t;             // 总大小: 4+4+64+64 = 136B，对齐到 128B
```

操作类型（op_type）：

| op_type | 名称 | 说明 |
|---------|------|------|
| 0x01 | TCAM_WRITE | 写入TCAM条目（data+mask） |
| 0x02 | TCAM_INVALIDATE | 使TCAM条目无效 |
| 0x03 | ASRAM_WRITE | 写入Action SRAM |
| 0x04 | SSRAM_WRITE | 写入Stateful SRAM |
| 0x05 | PARSER_TCAM_WRITE | 写入Parser TCAM |
| 0x06 | TM_CFG_WRITE | 写入TM配置寄存器 |
| 0x07 | MCAST_WRITE | 写入组播表 |
| 0x08 | BATCH_COMMIT | 提交批量事务（触发Swap） |
| 0x09 | COUNTER_CLEAR | 清零计数器 |
| 0x0A | METER_CFG | 配置Meter参数 |

## 4.4 APB 寄存器映射表

TUE 通过 APB 总线暴露给 RISC-V CPU，基地址为 `0x4000_0000`。

| 偏移 | 寄存器名 | 位域 | 访问 | 说明 |
|------|----------|------|------|------|
| 0x000 | TUE_CTRL | [0]: enable<br>[1]: reset<br>[2]: drain_wait<br>[7:4]: reserved | RW | TUE全局控制 |
| 0x004 | TUE_STATUS | [0]: busy<br>[1]: queue_full<br>[2]: queue_empty<br>[3]: error<br>[7:4]: queue_depth[5:0] | RO | TUE状态 |
| 0x008 | TUE_INT_EN | [0]: done_int_en<br>[1]: err_int_en<br>[2]: full_int_en | RW | 中断使能 |
| 0x00C | TUE_INT_STATUS | [0]: done<br>[1]: error<br>[2]: queue_full | RW1C | 中断状态（写1清零） |
| 0x010 | TXN_OP_TYPE | [7:0]: op_type | WO | 事务操作类型 |
| 0x014 | TXN_STAGE_ID | [4:0]: stage_id | WO | 目标Stage |
| 0x018 | TXN_TABLE_TYPE | [3:0]: table_type | WO | 目标表类型 |
| 0x01C | TXN_FLAGS | [7:0]: flags | WO | 事务标志 |
| 0x020 | TXN_ADDR | [31:0]: addr | WO | 目标地址 |
| 0x024-0x063 | TXN_DATA[0..15] | [31:0]: data | WO | 写入数据（16×4B=64B） |
| 0x064-0x0A3 | TXN_MASK[0..15] | [31:0]: mask | WO | TCAM掩码（16×4B=64B） |
| 0x0A4 | TXN_SUBMIT | [0]: submit | WO | 写1提交事务到队列 |
| 0x0A8 | TXN_BATCH_COMMIT | [0]: commit | WO | 写1提交批量事务 |
| 0x0AC | TUE_QUEUE_DEPTH | [5:0]: depth | RO | 当前队列深度 |
| 0x0B0 | TUE_DONE_COUNT | [31:0]: count | RO | 已完成事务计数 |
| 0x0B4 | TUE_ERR_COUNT | [31:0]: count | RO | 错误事务计数 |
| 0x0B8 | TUE_DRAIN_TIMEOUT | [15:0]: timeout_cycles | RW | Drain等待超时（cycles） |
| 0x0BC | TUE_VERSION | [31:0]: version | RO | TUE版本号 |
| 0x100 | STAGE_ACTIVE_MASK | [23:0]: mask | RW | 各Stage使能掩码 |
| 0x104 | PARSER_CFG | [5:0]: state_count<br>[7:6]: reserved | RW | Parser配置 |
| 0x108 | TM_PORT_SPEED[0] | [3:0]: speed | RW | 端口0速率配置 |
| 0x10C | TM_PORT_SPEED[1] | [3:0]: speed | RW | 端口1速率配置 |
| ... | ... | ... | ... | ... |
| 0x184 | TM_PORT_SPEED[31] | [3:0]: speed | RW | 端口31速率配置 |
| 0x200 | METER_BASE | - | - | Meter配置基地址（见下） |
| 0x200+n×16 | METER_CFG_n | [31:0]: cir | RW | Meter n承诺信息速率（bps） |
| 0x204+n×16 | METER_CBS_n | [31:0]: cbs | RW | Meter n承诺突发大小（bytes） |
| 0x208+n×16 | METER_PIR_n | [31:0]: pir | RW | Meter n峰值信息速率（bps） |
| 0x20C+n×16 | METER_PBS_n | [31:0]: pbs | RW | Meter n峰值突发大小（bytes） |
| 0x1000 | COUNTER_BASE | - | - | 计数器基地址 |
| 0x1000+n×8 | COUNTER_n_LO | [31:0]: count_lo | RO | 计数器n低32位 |
| 0x1004+n×8 | COUNTER_n_HI | [31:0]: count_hi | RO | 计数器n高32位 |

## 4.5 跨时钟域处理

TUE 需要处理控制面时钟（clk_ctrl = 1.5 GHz）到数据面时钟（clk_dp = 1 GHz）的跨时钟域问题。

```
clk_ctrl 域（1.5 GHz）          clk_dp 域（1 GHz）
─────────────────────           ──────────────────────
APB接口                          TCAM/SRAM写接口
事务队列（写侧）                  事务队列（读侧）
Shadow Buffer写                  Shadow Buffer读
                │                        ▲
                │   跨时钟域同步          │
                ▼                        │
         ┌─────────────────────────────────┐
         │   异步FIFO（Gray码指针）         │
         │   深度: 16 entry                │
         │   写侧: clk_ctrl                │
         │   读侧: clk_dp                  │
         └─────────────────────────────────┘

控制信号同步（单bit）：
  • 使用2级触发器同步（2FF synchronizer）
  • 适用于：enable、reset、drain_req等慢变信号

数据总线同步：
  • 通过异步FIFO传递（Gray码计数器）
  • 写满/读空信号经2FF同步回各自时钟域
```

## 4.6 TUE 接口信号表

| 信号名 | 方向 | 宽度 | 时钟域 | 说明 |
|--------|------|------|--------|------|
| clk_ctrl | input | 1 | - | 控制面时钟（1.5 GHz） |
| clk_dp | input | 1 | - | 数据面时钟（1 GHz） |
| rst_ctrl_n | input | 1 | - | 控制面复位 |
| rst_dp_n | input | 1 | - | 数据面复位 |
| apb_paddr | input | 16 | clk_ctrl | APB地址 |
| apb_pwdata | input | 32 | clk_ctrl | APB写数据 |
| apb_pwrite | input | 1 | clk_ctrl | APB写使能 |
| apb_psel | input | 1 | clk_ctrl | APB片选 |
| apb_penable | input | 1 | clk_ctrl | APB使能 |
| apb_prdata | output | 32 | clk_ctrl | APB读数据 |
| apb_pready | output | 1 | clk_ctrl | APB就绪 |
| apb_pslverr | output | 1 | clk_ctrl | APB错误 |
| tcam_wr_addr | output | 11 | clk_dp | TCAM写地址 |
| tcam_wr_data | output | 512 | clk_dp | TCAM写数据 |
| tcam_wr_mask | output | 512 | clk_dp | TCAM写掩码 |
| tcam_wr_en | output | 1 | clk_dp | TCAM写使能 |
| tcam_stage_sel | output | 5 | clk_dp | 目标Stage选择 |
| asram_wr_addr | output | 16 | clk_dp | ASRAM写地址 |
| asram_wr_data | output | 128 | clk_dp | ASRAM写数据 |
| asram_wr_en | output | 1 | clk_dp | ASRAM写使能 |
| irq_out | output | 1 | clk_ctrl | 中断输出到PLIC |
| drain_req_out | output | 1 | clk_dp | 请求数据面drain |
| drain_ack_in | input | 1 | clk_dp | 数据面drain完成确认 |

---

# 五、数据面编程模型（C-to-HW 编译器）

## 5.1 编译器架构

RV-P4 C 编译器基于 LLVM 框架构建，将带有特殊标注的 C 代码编译为硬件配置文件。

```
┌─────────────────────────────────────────────────────────────────────┐
│                    RV-P4 C 编译器架构                                │
│                                                                     │
│  输入: dataplane.c（带标注的C代码）                                  │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  前端（Frontend）                                             │  │
│  │  ├── Clang C Parser（标准C11解析）                            │  │
│  │  ├── 标注识别器（__attribute__((rvp4_parser/table/action))）  │  │
│  │  ├── 类型检查器（PHV字段类型验证）                             │  │
│  │  └── 约束验证器（C子集约束检查）                               │  │
│  └──────────────────────────┬─────────────────────────────────┘  │
│                             │ LLVM IR                              │
│  ┌──────────────────────────▼─────────────────────────────────┐  │
│  │  中间优化（Middle-end）                                       │  │
│  │  ├── 解析图构建（Parse Graph Construction）                   │  │
│  │  ├── 表依赖分析（Table Dependency Analysis）                  │  │
│  │  ├── PHV字段分配（PHV Field Allocation）                      │  │
│  │  ├── Stage分配（Stage Assignment，贪心算法）                  │  │
│  │  └── 动作原语提取（Action Primitive Extraction）              │  │
│  └──────────────────────────┬─────────────────────────────────┘  │
│                             │ RV-P4 IR                             │
│  ┌──────────────────────────▼─────────────────────────────────┐  │
│  │  后端（Backend）                                             │  │
│  │  ├── Parser FSM 生成器 → parser_tcam.bin                    │  │
│  │  ├── MAU TCAM 初始化镜像生成器 → mau_tcam_init.bin          │  │
│  │  ├── Action SRAM 初始化镜像生成器 → mau_asram_init.bin      │  │
│  │  ├── PHV 字段映射表生成器 → phv_map.json                    │  │
│  │  └── 硬件配置文件打包器 → dataplane.hwcfg                   │  │
│  └──────────────────────────┬─────────────────────────────────┘  │
│                             │                                      │
│  输出: dataplane.hwcfg（硬件配置包）                                │
└─────────────────────────────────────────────────────────────────────┘
```

## 5.2 支持的 C 语法标注

编译器通过 GCC 兼容的 `__attribute__` 扩展识别数据面语义：

| 标注 | 作用域 | 说明 |
|------|--------|------|
| `__attribute__((rvp4_parser))` | 函数 | 标记为Parser状态处理函数 |
| `__attribute__((rvp4_table))` | 函数 | 标记为MAU表查找函数 |
| `__attribute__((rvp4_action))` | 函数 | 标记为动作函数 |
| `__attribute__((rvp4_phv_field))` | 结构体成员 | 标记为PHV字段 |
| `__attribute__((rvp4_metadata))` | 结构体成员 | 标记为元数据字段 |
| `__attribute__((rvp4_stage(N)))` | 函数 | 指定分配到Stage N |
| `__attribute__((rvp4_priority(N)))` | 表条目 | 指定TCAM优先级 |
| `__attribute__((rvp4_exact))` | 表 | 精确匹配（Hash表） |
| `__attribute__((rvp4_lpm))` | 表 | 最长前缀匹配（TCAM） |
| `__attribute__((rvp4_ternary))` | 表 | 三值匹配（TCAM） |
| `__attribute__((rvp4_size(N)))` | 表 | 指定表大小（条目数） |

## 5.3 C 子集约束表

数据面 C 代码受以下约束限制（编译器强制检查）：

| 约束类别 | 允许 | 禁止 | 说明 |
|----------|------|------|------|
| 数据类型 | uint8/16/32/64_t, bool | float, double, 指针（除PHV引用外） | 硬件不支持浮点 |
| 控制流 | if/else, switch | for/while/do-while（Parser循环除外） | 循环需展开 |
| 函数调用 | 标注函数间调用 | 递归, 函数指针 | 硬件无调用栈 |
| 内存访问 | PHV字段访问, Stateful SRAM | 动态内存分配, 全局变量写 | 无堆内存 |
| 算术运算 | +, -, &, \|, ^, ~, <<, >> | /, % | 硬件无除法器 |
| 比较运算 | ==, !=, <, >, <=, >= | - | 全部支持 |
| 位宽 | 最大64b单次操作 | 超过64b的单次运算 | ALU宽度限制 |
| 表大小 | 最大2048条目/Stage | 超过2048 | TCAM物理限制 |
| 动作复杂度 | 最多8个原语/动作 | 超过8个原语 | Action Word宽度限制 |
| 嵌套深度 | 最多4层if嵌套 | 超过4层 | TCAM条目数限制 |

## 5.4 编译器输出文件说明

| 文件名 | 格式 | 说明 |
|--------|------|------|
| `parser_tcam.bin` | 二进制 | Parser TCAM初始化镜像（64×80B） |
| `mau_tcam_init_s{N}.bin` | 二进制 | Stage N的TCAM初始化镜像 |
| `mau_asram_init_s{N}.bin` | 二进制 | Stage N的Action SRAM初始化镜像 |
| `phv_map.json` | JSON | PHV字段ID到名称的映射表 |
| `table_info.json` | JSON | 表名、Stage分配、大小等元信息 |
| `action_info.json` | JSON | 动作名、操作码、参数类型 |
| `dataplane.hwcfg` | 压缩包 | 上述所有文件的打包（用于烧录） |
| `dataplane.report` | 文本 | 编译报告（资源使用、警告） |

## 5.5 完整 C 代码示例

以下示例展示了一个完整的数据面程序，包含 Parser 和三张表：

```c
/*
 * dataplane.c - RV-P4 数据面程序示例
 * 包含: Parser + ipv4_lpm + acl_ingress + l2_fdb
 */

#include <rvp4/rvp4.h>      /* RV-P4 内置类型和宏定义 */
#include <rvp4/phv.h>       /* PHV结构体定义 */
#include <rvp4/primitives.h>/* 动作原语 */

/* ================================================================
 * PHV 字段声明
 * ================================================================ */

/* 以太网头 */
typedef struct {
    uint8_t  dst_mac[6]  __attribute__((rvp4_phv_field));
    uint8_t  src_mac[6]  __attribute__((rvp4_phv_field));
    uint16_t ethertype   __attribute__((rvp4_phv_field));
} eth_hdr_t;

/* IPv4头 */
typedef struct {
    uint8_t  version_ihl __attribute__((rvp4_phv_field));
    uint8_t  dscp_ecn    __attribute__((rvp4_phv_field));
    uint16_t total_len   __attribute__((rvp4_phv_field));
    uint16_t id          __attribute__((rvp4_phv_field));
    uint16_t flags_frag  __attribute__((rvp4_phv_field));
    uint8_t  ttl         __attribute__((rvp4_phv_field));
    uint8_t  protocol    __attribute__((rvp4_phv_field));
    uint16_t checksum    __attribute__((rvp4_phv_field));
    uint32_t src_addr    __attribute__((rvp4_phv_field));
    uint32_t dst_addr    __attribute__((rvp4_phv_field));
} ipv4_hdr_t;

/* TCP头 */
typedef struct {
    uint16_t src_port    __attribute__((rvp4_phv_field));
    uint16_t dst_port    __attribute__((rvp4_phv_field));
    uint32_t seq_num     __attribute__((rvp4_phv_field));
    uint32_t ack_num     __attribute__((rvp4_phv_field));
    uint8_t  flags       __attribute__((rvp4_phv_field));
    uint16_t window      __attribute__((rvp4_phv_field));
    uint16_t checksum    __attribute__((rvp4_phv_field));
} tcp_hdr_t;

/* UDP头 */
typedef struct {
    uint16_t src_port    __attribute__((rvp4_phv_field));
    uint16_t dst_port    __attribute__((rvp4_phv_field));
    uint16_t length      __attribute__((rvp4_phv_field));
    uint16_t checksum    __attribute__((rvp4_phv_field));
} udp_hdr_t;

/* 元数据 */
typedef struct {
    uint16_t ingress_port __attribute__((rvp4_metadata));
    uint16_t egress_port  __attribute__((rvp4_metadata));
    uint32_t vrf_id       __attribute__((rvp4_metadata));
    uint32_t nexthop_id   __attribute__((rvp4_metadata));
    uint8_t  drop         __attribute__((rvp4_metadata));
    uint8_t  acl_color    __attribute__((rvp4_metadata));
    uint32_t hash_val     __attribute__((rvp4_metadata));
} meta_t;

/* 全局PHV实例（编译器将其映射到PHV内存） */
eth_hdr_t  eth;
ipv4_hdr_t ipv4;
tcp_hdr_t  tcp;
udp_hdr_t  udp;
meta_t     meta;

/* ================================================================
 * Parser 定义
 * ================================================================ */

/* 解析以太网头 */
__attribute__((rvp4_parser))
void parse_ethernet(void) {
    /* 提取以太网头字段 */
    extract(eth.dst_mac, 48);
    extract(eth.src_mac, 48);
    extract(eth.ethertype, 16);

    /* 根据ethertype决定下一状态 */
    switch (eth.ethertype) {
        case 0x0800: transition(parse_ipv4);  break;
        case 0x86DD: transition(parse_ipv6);  break;
        case 0x8100: transition(parse_vlan);  break;
        default:     transition(accept);      break;
    }
}

/* 解析IPv4头 */
__attribute__((rvp4_parser))
void parse_ipv4(void) {
    extract(ipv4.version_ihl, 8);
    extract(ipv4.dscp_ecn,    8);
    extract(ipv4.total_len,  16);
    extract(ipv4.id,         16);
    extract(ipv4.flags_frag, 16);
    extract(ipv4.ttl,         8);
    extract(ipv4.protocol,    8);
    extract(ipv4.checksum,   16);
    extract(ipv4.src_addr,   32);
    extract(ipv4.dst_addr,   32);

    switch (ipv4.protocol) {
        case 6:  transition(parse_tcp);  break;
        case 17: transition(parse_udp);  break;
        default: transition(accept);     break;
    }
}

/* 解析TCP头 */
__attribute__((rvp4_parser))
void parse_tcp(void) {
    extract(tcp.src_port, 16);
    extract(tcp.dst_port, 16);
    extract(tcp.seq_num,  32);
    extract(tcp.ack_num,  32);
    extract(tcp.flags,     8);
    extract(tcp.window,   16);
    extract(tcp.checksum, 16);
    transition(accept);
}

/* 解析UDP头 */
__attribute__((rvp4_parser))
void parse_udp(void) {
    extract(udp.src_port, 16);
    extract(udp.dst_port, 16);
    extract(udp.length,   16);
    extract(udp.checksum, 16);
    transition(accept);
}

/* ================================================================
 * 表1: ipv4_lpm - IPv4最长前缀匹配路由表
 * ================================================================ */

/* 动作: 设置下一跳并转发 */
__attribute__((rvp4_action))
void action_set_nexthop(uint16_t port, uint32_t nexthop_id) {
    meta.egress_port  = port;
    meta.nexthop_id   = nexthop_id;
    ipv4.ttl          = ipv4.ttl - 1;   /* TTL递减 */
}

/* 动作: 丢弃报文 */
__attribute__((rvp4_action))
void action_drop(void) {
    meta.drop = 1;
}

/* IPv4 LPM路由表定义 */
__attribute__((rvp4_table, rvp4_lpm, rvp4_size(4096), rvp4_stage(0)))
void ipv4_lpm(void) {
    /* 匹配键: 目的IP地址（LPM匹配） */
    key {
        ipv4.dst_addr : lpm;
    }
    /* 动作列表 */
    actions {
        action_set_nexthop;
        action_drop;
    }
    /* 默认动作: 丢弃 */
    default_action = action_drop;
}

/* ================================================================
 * 表2: acl_ingress - 入口ACL访问控制表
 * ================================================================ */

/* 动作: 允许通过 */
__attribute__((rvp4_action))
void action_permit(void) {
    meta.acl_color = 0;  /* 绿色 */
}

/* 动作: 拒绝并丢弃 */
__attribute__((rvp4_action))
void action_deny(void) {
    meta.drop = 1;
    meta.acl_color = 2;  /* 红色 */
}

/* 动作: 限速（设置Meter） */
__attribute__((rvp4_action))
void action_rate_limit(uint16_t meter_id) {
    uint8_t color = meter_execute(meter_id);
    if (color == 2) {    /* 红色: 丢弃 */
        meta.drop = 1;
    }
    meta.acl_color = color;
}

/* 入口ACL表定义 */
__attribute__((rvp4_table, rvp4_ternary, rvp4_size(2048), rvp4_stage(1)))
void acl_ingress(void) {
    /* 匹配键: 5元组（三值匹配，支持通配符） */
    key {
        ipv4.src_addr  : ternary;
        ipv4.dst_addr  : ternary;
        ipv4.protocol  : ternary;
        tcp.src_port   : ternary;
        tcp.dst_port   : ternary;
        meta.ingress_port : ternary;
    }
    actions {
        action_permit;
        action_deny;
        action_rate_limit;
    }
    default_action = action_permit;
}

/* ================================================================
 * 表3: l2_fdb - L2转发数据库（MAC地址表）
 * ================================================================ */

/* 动作: L2单播转发 */
__attribute__((rvp4_action))
void action_l2_forward(uint16_t port) {
    meta.egress_port = port;
}

/* 动作: L2泛洪（广播到所有端口） */
__attribute__((rvp4_action))
void action_l2_flood(uint16_t mcast_grp_id) {
    meta.egress_port = MCAST_PORT;
    set_multicast_group(mcast_grp_id);
}

/* L2 FDB表定义（精确匹配，Hash表实现） */
__attribute__((rvp4_table, rvp4_exact, rvp4_size(65536), rvp4_stage(2)))
void l2_fdb(void) {
    /* 匹配键: 目的MAC + VLAN ID（精确匹配） */
    key {
        eth.dst_mac  : exact;
        meta.vrf_id  : exact;   /* 复用vrf_id字段存储VLAN ID */
    }
    actions {
        action_l2_forward;
        action_l2_flood;
    }
    default_action = action_l2_flood(0);  /* 默认泛洪到组播组0 */
}

/* ================================================================
 * 控制流（表执行顺序）
 * ================================================================ */
void ingress_control(void) {
    /* 先执行L2 FDB查找 */
    apply(l2_fdb);

    /* 若目的IP有效，执行IPv4路由 */
    if (eth.ethertype == 0x0800) {
        apply(ipv4_lpm);
    }

    /* 执行入口ACL（最后，可覆盖路由决策） */
    apply(acl_ingress);
}
```

---

# 六、控制面编程模型（RISC-V C SDK）

## 6.1 HAL 完整接口定义

HAL（Hardware Abstraction Layer）提供控制面 C 程序操作硬件表的统一接口，运行在 XiangShan RISC-V 核心上。

### 6.1.1 初始化接口

```c
/* 初始化HAL，加载硬件配置文件 */
int hal_init(const char *hwcfg_path);

/* 反初始化HAL，释放资源 */
void hal_deinit(void);

/* 获取HAL版本号 */
uint32_t hal_get_version(void);

/* 复位指定模块 */
int hal_module_reset(uint8_t module_id);

/* 获取芯片ID */
uint32_t hal_get_chip_id(void);
```

### 6.1.2 TCAM 操作接口

```c
/*
 * 向指定Stage的TCAM插入一条条目
 * stage_id: 目标Stage（0-23）
 * entry:    TCAM条目（含key/mask/priority/action_addr）
 * 返回值:   0=成功，负数=错误码
 */
int hal_tcam_insert(uint8_t stage_id, const tcam_entry_t *entry);

/*
 * 删除指定Stage TCAM中的一条条目
 * stage_id: 目标Stage（0-23）
 * index:    条目索引（0-2047）
 * 返回值:   0=成功，负数=错误码
 */
int hal_tcam_delete(uint8_t stage_id, uint16_t index);

/*
 * 更新指定Stage TCAM中的一条条目（原子操作）
 * stage_id: 目标Stage（0-23）
 * index:    条目索引（0-2047）
 * entry:    新的TCAM条目
 * 返回值:   0=成功，负数=错误码
 */
int hal_tcam_update(uint8_t stage_id, uint16_t index,
                    const tcam_entry_t *entry);

/*
 * 读取指定Stage TCAM中的一条条目
 * stage_id: 目标Stage（0-23）
 * index:    条目索引（0-2047）
 * entry:    输出缓冲区
 * 返回值:   0=成功，负数=错误码
 */
int hal_tcam_read(uint8_t stage_id, uint16_t index,
                  tcam_entry_t *entry);

/*
 * 批量插入TCAM条目（事务性，全部成功或全部失败）
 * stage_id:    目标Stage
 * entries:     条目数组
 * count:       条目数量
 * 返回值:      0=成功，负数=错误码
 */
int hal_tcam_batch_insert(uint8_t stage_id,
                          const tcam_entry_t *entries,
                          uint32_t count);

/* 清空指定Stage的所有TCAM条目 */
int hal_tcam_flush(uint8_t stage_id);
```

### 6.1.3 路由管理接口

```c
/*
 * 添加IPv4路由条目
 * prefix:    目的网络地址（网络字节序）
 * prefix_len: 前缀长度（0-32）
 * nexthop:   下一跳IP地址
 * port:      出口端口号（0-31）
 * vrf_id:    VRF标识（0=默认VRF）
 * 返回值:    0=成功，负数=错误码
 */
int hal_route_add(uint32_t prefix, uint8_t prefix_len,
                  uint32_t nexthop, uint16_t port,
                  uint32_t vrf_id);

/*
 * 删除IPv4路由条目
 * prefix:    目的网络地址
 * prefix_len: 前缀长度
 * vrf_id:    VRF标识
 * 返回值:    0=成功，负数=错误码
 */
int hal_route_del(uint32_t prefix, uint8_t prefix_len,
                  uint32_t vrf_id);

/*
 * 查询IPv4路由条目
 * prefix:    目的网络地址
 * prefix_len: 前缀长度
 * vrf_id:    VRF标识
 * result:    输出：匹配的路由条目
 * 返回值:    0=找到，-ENOENT=未找到
 */
int hal_route_lookup(uint32_t prefix, uint8_t prefix_len,
                     uint32_t vrf_id, route_entry_t *result);

/* 获取路由表统计信息 */
int hal_route_get_stats(route_stats_t *stats);

/* 添加ECMP组 */
int hal_ecmp_group_add(uint32_t group_id,
                       const ecmp_member_t *members,
                       uint8_t member_count);

/* 删除ECMP组 */
int hal_ecmp_group_del(uint32_t group_id);
```

### 6.1.4 ACL 管理接口

```c
/*
 * 添加ACL规则
 * rule:    ACL规则（含匹配键、掩码、动作、优先级）
 * 返回值:  规则ID（>=0），负数=错误码
 */
int hal_acl_add(const acl_rule_t *rule);

/*
 * 删除ACL规则
 * rule_id: 规则ID（hal_acl_add返回值）
 * 返回值:  0=成功，负数=错误码
 */
int hal_acl_del(int rule_id);

/*
 * 更新ACL规则动作（不改变匹配键）
 * rule_id: 规则ID
 * action:  新动作
 * 返回值:  0=成功，负数=错误码
 */
int hal_acl_update_action(int rule_id, const acl_action_t *action);

/* 获取ACL规则命中计数 */
int hal_acl_get_hit_count(int rule_id, uint64_t *count);

/* 清零ACL规则命中计数 */
int hal_acl_clear_hit_count(int rule_id);
```

### 6.1.5 端口管理接口

```c
/*
 * 配置端口参数
 * port_id: 端口号（0-31）
 * cfg:     端口配置（速率/双工/自协商等）
 * 返回值:  0=成功，负数=错误码
 */
int hal_port_config(uint8_t port_id, const port_cfg_t *cfg);

/*
 * 获取端口状态
 * port_id: 端口号
 * status:  输出：端口状态
 * 返回值:  0=成功，负数=错误码
 */
int hal_port_get_status(uint8_t port_id, port_status_t *status);

/*
 * 获取端口统计信息
 * port_id: 端口号
 * stats:   输出：端口统计
 * 返回值:  0=成功，负数=错误码
 */
int hal_port_get_stats(uint8_t port_id, port_stats_t *stats);

/* 清零端口统计 */
int hal_port_clear_stats(uint8_t port_id);

/* 使能/禁用端口 */
int hal_port_enable(uint8_t port_id, bool enable);

/* 配置端口VLAN */
int hal_port_set_vlan(uint8_t port_id, uint16_t pvid,
                      port_vlan_mode_t mode);
```

### 6.1.6 Meter 接口

```c
/*
 * 配置Meter参数（双速率三色标记，RFC 2698）
 * meter_id: Meter ID（0-4095）
 * cfg:      Meter配置
 * 返回值:   0=成功，负数=错误码
 */
int hal_meter_set(uint16_t meter_id, const meter_cfg_t *cfg);

/* 读取Meter当前状态（令牌桶水位） */
int hal_meter_get_state(uint16_t meter_id, meter_state_t *state);

/* 重置Meter（清空令牌桶） */
int hal_meter_reset(uint16_t meter_id);
```

### 6.1.7 L2 FDB 接口

```c
/* 添加静态MAC表项 */
int hal_fdb_add(const uint8_t *mac, uint16_t vlan_id,
                uint16_t port_id, bool is_static);

/* 删除MAC表项 */
int hal_fdb_del(const uint8_t *mac, uint16_t vlan_id);

/* 查询MAC表项 */
int hal_fdb_lookup(const uint8_t *mac, uint16_t vlan_id,
                   fdb_entry_t *entry);

/* 老化扫描（由定时器调用） */
int hal_fdb_age_scan(uint32_t age_timeout_sec);

/* 获取FDB统计 */
int hal_fdb_get_stats(fdb_stats_t *stats);
```

### 6.1.8 组播接口

```c
/* 创建组播组 */
int hal_mcast_group_create(uint16_t group_id);

/* 删除组播组 */
int hal_mcast_group_delete(uint16_t group_id);

/* 向组播组添加成员端口 */
int hal_mcast_member_add(uint16_t group_id, uint8_t port_id,
                         uint8_t queue_id);

/* 从组播组删除成员端口 */
int hal_mcast_member_del(uint16_t group_id, uint8_t port_id);
```

## 6.2 数据结构定义

```c
/* ================================================================
 * TCAM 条目
 * ================================================================ */
typedef struct {
    uint8_t  key[64];        /* 匹配键（最大512b = 64B） */
    uint8_t  mask[64];       /* 掩码（0=don't care, 1=精确匹配） */
    uint16_t priority;       /* 优先级（0=最高） */
    uint16_t action_addr;    /* Action SRAM地址 */
    uint8_t  valid;          /* 条目有效标志 */
    uint8_t  reserved[3];    /* 对齐填充 */
} tcam_entry_t;              /* 总大小: 136B */

/* ================================================================
 * Meter 配置（双速率三色，RFC 2698）
 * ================================================================ */
typedef struct {
    uint32_t cir;            /* 承诺信息速率（bps） */
    uint32_t cbs;            /* 承诺突发大小（bytes） */
    uint32_t pir;            /* 峰值信息速率（bps） */
    uint32_t pbs;            /* 峰值突发大小（bytes） */
    uint8_t  color_aware;    /* 颜色感知模式（0=盲，1=感知） */
    uint8_t  reserved[3];
} meter_cfg_t;               /* 总大小: 20B */

/* ================================================================
 * 端口统计
 * ================================================================ */
typedef struct {
    uint64_t rx_pkts;        /* 接收报文数 */
    uint64_t rx_bytes;       /* 接收字节数 */
    uint64_t rx_errors;      /* 接收错误数 */
    uint64_t rx_drops;       /* 接收丢包数 */
    uint64_t tx_pkts;        /* 发送报文数 */
    uint64_t tx_bytes;       /* 发送字节数 */
    uint64_t tx_errors;      /* 发送错误数 */
    uint64_t tx_drops;       /* 发送丢包数 */
    uint64_t rx_unicast;     /* 接收单播数 */
    uint64_t rx_multicast;   /* 接收组播数 */
    uint64_t rx_broadcast;   /* 接收广播数 */
    uint64_t tx_unicast;     /* 发送单播数 */
    uint64_t tx_multicast;   /* 发送组播数 */
    uint64_t tx_broadcast;   /* 发送广播数 */
    uint64_t rx_pause;       /* 接收PAUSE帧数 */
    uint64_t tx_pause;       /* 发送PAUSE帧数 */
} port_stats_t;              /* 总大小: 128B */

/* ================================================================
 * 端口配置
 * ================================================================ */
typedef struct {
    uint32_t speed;          /* 端口速率（Mbps）: 100000/40000/25000/10000/1000 */
    uint8_t  duplex;         /* 双工模式: 0=半双工, 1=全双工 */
    uint8_t  autoneg;        /* 自协商: 0=禁用, 1=使能 */
    uint8_t  fec;            /* FEC模式: 0=无, 1=RS-FEC, 2=BASE-R FEC */
    uint8_t  loopback;       /* 环回模式: 0=正常, 1=MAC环回, 2=PHY环回 */
    uint16_t mtu;            /* MTU（字节，最大9600） */
    uint8_t  flow_ctrl_rx;   /* 接收流控: 0=禁用, 1=使能 */
    uint8_t  flow_ctrl_tx;   /* 发送流控: 0=禁用, 1=使能 */
} port_cfg_t;                /* 总大小: 12B */

/* ================================================================
 * ACL 规则
 * ================================================================ */
typedef struct {
    /* 匹配键 */
    uint32_t src_ip;         /* 源IP */
    uint32_t src_ip_mask;    /* 源IP掩码 */
    uint32_t dst_ip;         /* 目的IP */
    uint32_t dst_ip_mask;    /* 目的IP掩码 */
    uint16_t src_port;       /* 源端口 */
    uint16_t src_port_mask;  /* 源端口掩码 */
    uint16_t dst_port;       /* 目的端口 */
    uint16_t dst_port_mask;  /* 目的端口掩码 */
    uint8_t  protocol;       /* 协议号 */
    uint8_t  protocol_mask;  /* 协议掩码 */
    uint16_t ingress_port;   /* 入口端口 */
    uint16_t ingress_port_mask;
    /* 动作 */
    uint8_t  action;         /* 0=permit, 1=deny, 2=rate_limit */
    uint16_t meter_id;       /* Meter ID（action=rate_limit时有效） */
    /* 优先级 */
    uint16_t priority;       /* 规则优先级（0=最高） */
} acl_rule_t;                /* 总大小: 32B */

/* ================================================================
 * 路由条目
 * ================================================================ */
typedef struct {
    uint32_t prefix;         /* 目的网络地址 */
    uint8_t  prefix_len;     /* 前缀长度 */
    uint32_t nexthop;        /* 下一跳IP */
    uint16_t port;           /* 出口端口 */
    uint32_t vrf_id;         /* VRF标识 */
    uint8_t  ecmp_group;     /* ECMP组ID（0=不使用ECMP） */
    uint32_t hit_count;      /* 命中计数 */
} route_entry_t;             /* 总大小: 20B */

/* ================================================================
 * FDB 条目
 * ================================================================ */
typedef struct {
    uint8_t  mac[6];         /* MAC地址 */
    uint16_t vlan_id;        /* VLAN ID */
    uint16_t port_id;        /* 出口端口 */
    uint8_t  is_static;      /* 静态条目标志 */
    uint32_t age_timer;      /* 老化计时器（秒） */
} fdb_entry_t;               /* 总大小: 16B */
```

## 6.3 控制面 C 代码完整示例

```c
/*
 * control_plane.c - RV-P4 控制面程序示例
 * 运行在 XiangShan RISC-V 核心上
 * 编译: riscv64-unknown-elf-gcc -O2 -march=rv64gc -mabi=lp64d
 *       -I$(SDK_ROOT)/include -L$(SDK_ROOT)/lib -lrvp4hal
 *       control_plane.c -o control_plane.elf
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <rvp4/hal.h>
#include <rvp4/types.h>

/* ================================================================
 * 路由管理示例
 * ================================================================ */

/* 初始化默认路由表 */
static int init_routing_table(void)
{
    int ret;

    /* 添加默认路由 0.0.0.0/0 → 端口0，下一跳 192.168.1.1 */
    ret = hal_route_add(0x00000000, 0,
                        0xC0A80101,  /* 192.168.1.1 */
                        0,           /* 端口0 */
                        0);          /* 默认VRF */
    if (ret < 0) {
        printf("ERROR: 添加默认路由失败: %d\n", ret);
        return ret;
    }

    /* 添加主机路由 10.0.0.1/32 → 端口1 */
    ret = hal_route_add(0x0A000001, 32,
                        0x0A000001,
                        1,
                        0);
    if (ret < 0) {
        printf("ERROR: 添加主机路由失败: %d\n", ret);
        return ret;
    }

    /* 添加网段路由 10.1.0.0/16 → 端口2 */
    ret = hal_route_add(0x0A010000, 16,
                        0x0A010001,
                        2,
                        0);
    if (ret < 0) {
        printf("ERROR: 添加网段路由失败: %d\n", ret);
        return ret;
    }

    /* 添加ECMP路由 172.16.0.0/12 → ECMP组1（端口3,4,5） */
    ecmp_member_t ecmp_members[3] = {
        {.port = 3, .weight = 1},
        {.port = 4, .weight = 1},
        {.port = 5, .weight = 2},  /* 端口5权重2倍 */
    };
    ret = hal_ecmp_group_add(1, ecmp_members, 3);
    if (ret < 0) {
        printf("ERROR: 创建ECMP组失败: %d\n", ret);
        return ret;
    }

    printf("INFO: 路由表初始化完成\n");
    return 0;
}

/* ================================================================
 * ACL 管理示例
 * ================================================================ */

/* 初始化ACL规则 */
static int init_acl_rules(void)
{
    int rule_id;
    acl_rule_t rule;

    /* 规则1: 拒绝来自 192.168.100.0/24 的所有流量（优先级0，最高） */
    memset(&rule, 0, sizeof(rule));
    rule.src_ip        = 0xC0A86400;  /* 192.168.100.0 */
    rule.src_ip_mask   = 0xFFFFFF00;  /* /24 */
    rule.dst_ip        = 0x00000000;
    rule.dst_ip_mask   = 0x00000000;  /* 任意目的IP */
    rule.protocol      = 0x00;
    rule.protocol_mask = 0x00;        /* 任意协议 */
    rule.action        = 1;           /* deny */
    rule.priority      = 0;

    rule_id = hal_acl_add(&rule);
    if (rule_id < 0) {
        printf("ERROR: 添加ACL规则1失败: %d\n", rule_id);
        return rule_id;
    }
    printf("INFO: ACL规则1已添加，ID=%d\n", rule_id);

    /* 规则2: 对 TCP 80端口流量限速 10Mbps（优先级100） */
    memset(&rule, 0, sizeof(rule));
    rule.dst_port      = 80;
    rule.dst_port_mask = 0xFFFF;      /* 精确匹配端口80 */
    rule.protocol      = 6;           /* TCP */
    rule.protocol_mask = 0xFF;
    rule.action        = 2;           /* rate_limit */
    rule.meter_id      = 0;           /* 使用Meter 0 */
    rule.priority      = 100;

    /* 先配置Meter 0: CIR=10Mbps, CBS=64KB, PIR=20Mbps, PBS=128KB */
    meter_cfg_t meter = {
        .cir = 10000000,   /* 10 Mbps */
        .cbs = 65536,      /* 64 KB */
        .pir = 20000000,   /* 20 Mbps */
        .pbs = 131072,     /* 128 KB */
        .color_aware = 0,
    };
    int ret = hal_meter_set(0, &meter);
    if (ret < 0) {
        printf("ERROR: 配置Meter失败: %d\n", ret);
        return ret;
    }

    rule_id = hal_acl_add(&rule);
    if (rule_id < 0) {
        printf("ERROR: 添加ACL规则2失败: %d\n", rule_id);
        return rule_id;
    }
    printf("INFO: ACL规则2已添加，ID=%d\n", rule_id);

    return 0;
}

/* ================================================================
 * 端口管理示例
 * ================================================================ */

/* 初始化所有端口 */
static int init_ports(void)
{
    int ret;
    port_cfg_t cfg;

    /* 配置端口0-7为100GbE，RS-FEC，全双工 */
    for (int i = 0; i < 8; i++) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.speed       = 100000;  /* 100 Gbps */
        cfg.duplex      = 1;       /* 全双工 */
        cfg.autoneg     = 0;       /* 禁用自协商 */
        cfg.fec         = 1;       /* RS-FEC */
        cfg.mtu         = 9600;    /* Jumbo Frame */
        cfg.flow_ctrl_rx = 1;
        cfg.flow_ctrl_tx = 1;

        ret = hal_port_config(i, &cfg);
        if (ret < 0) {
            printf("ERROR: 配置端口%d失败: %d\n", i, ret);
            return ret;
        }

        ret = hal_port_enable(i, true);
        if (ret < 0) {
            printf("ERROR: 使能端口%d失败: %d\n", i, ret);
            return ret;
        }
    }

    printf("INFO: 端口0-7初始化完成（100GbE）\n");
    return 0;
}

/* 打印端口统计信息 */
static void print_port_stats(uint8_t port_id)
{
    port_stats_t stats;
    int ret = hal_port_get_stats(port_id, &stats);
    if (ret < 0) {
        printf("ERROR: 获取端口%d统计失败\n", port_id);
        return;
    }

    printf("端口%d统计:\n", port_id);
    printf("  RX: %llu pkts, %llu bytes, %llu drops\n",
           (unsigned long long)stats.rx_pkts,
           (unsigned long long)stats.rx_bytes,
           (unsigned long long)stats.rx_drops);
    printf("  TX: %llu pkts, %llu bytes, %llu drops\n",
           (unsigned long long)stats.tx_pkts,
           (unsigned long long)stats.tx_bytes,
           (unsigned long long)stats.tx_drops);
}

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void)
{
    int ret;

    printf("RV-P4 控制面启动...\n");

    /* 初始化HAL，加载数据面硬件配置 */
    ret = hal_init("/boot/dataplane.hwcfg");
    if (ret < 0) {
        printf("FATAL: HAL初始化失败: %d\n", ret);
        return -1;
    }
    printf("INFO: HAL初始化完成，版本=0x%08X\n", hal_get_version());

    /* 初始化端口 */
    ret = init_ports();
    if (ret < 0) goto cleanup;

    /* 初始化路由表 */
    ret = init_routing_table();
    if (ret < 0) goto cleanup;

    /* 初始化ACL规则 */
    ret = init_acl_rules();
    if (ret < 0) goto cleanup;

    printf("INFO: 系统初始化完成，进入主循环\n");

    /* 主循环：定期打印统计、处理老化等 */
    uint32_t tick = 0;
    while (1) {
        /* 每10秒打印一次端口0统计 */
        if (tick % 10 == 0) {
            print_port_stats(0);
        }

        /* 每60秒执行一次FDB老化扫描 */
        if (tick % 60 == 0) {
            hal_fdb_age_scan(300);  /* 300秒老化时间 */
        }

        /* 简单延时（实际应使用CLINT定时器中断） */
        for (volatile int i = 0; i < 1500000; i++);
        tick++;
    }

cleanup:
    hal_deinit();
    return ret;
}
```

## 6.4 工具链编译命令

```bash
# 工具链前缀
CROSS = riscv64-unknown-elf-

# 编译数据面（生成硬件配置文件）
rvp4-cc -O2 -target rvp4-hw \
    -I$(SDK_ROOT)/include \
    dataplane.c \
    -o dataplane.hwcfg

# 编译控制面（生成RISC-V ELF）
$(CROSS)gcc -O2 \
    -march=rv64gc \
    -mabi=lp64d \
    -mcmodel=medany \
    -I$(SDK_ROOT)/include \
    -L$(SDK_ROOT)/lib \
    -T$(SDK_ROOT)/linker/rvp4_ctrl.ld \
    control_plane.c \
    -lrvp4hal \
    -lc \
    -o control_plane.elf

# 生成二进制镜像（用于烧录）
$(CROSS)objcopy -O binary control_plane.elf control_plane.bin

# 查看编译报告
rvp4-cc --report dataplane.hwcfg

# 烧录到芯片（通过JTAG）
rvp4-flash --jtag \
    --hwcfg dataplane.hwcfg \
    --firmware control_plane.bin \
    --device /dev/ttyUSB0
```

---

# 七、关键数据结构

## 7.1 PHV 完整 C Struct 定义

```c
/*
 * phv.h - Packet Header Vector 完整定义
 * 总大小: 512 字节
 * 对齐:   64字节对齐（cache line对齐）
 */

#ifndef RVP4_PHV_H
#define RVP4_PHV_H

#include <stdint.h>

/* ---- 以太网头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} phv_eth_t;                 /* 14B */

/* ---- VLAN 标签 ---- */
typedef struct __attribute__((packed)) {
    uint16_t tci;            /* PCP[15:13] DEI[12] VID[11:0] */
    uint16_t ethertype;
} phv_vlan_t;                /* 4B */

/* ---- QinQ 双层VLAN ---- */
typedef struct __attribute__((packed)) {
    uint16_t outer_tci;
    uint16_t inner_tci;
    uint16_t ethertype;
} phv_qinq_t;                /* 6B */

/* ---- IPv4 头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  version_ihl;    /* version[7:4] ihl[3:0] */
    uint8_t  dscp_ecn;       /* dscp[7:2] ecn[1:0] */
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag_off; /* flags[15:13] frag_off[12:0] */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} phv_ipv4_t;                /* 20B */

/* ---- IPv6 头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  version_tc_fl[4]; /* version[31:28] tc[27:20] fl[19:0] */
    uint16_t payload_len;
    uint8_t  next_hdr;
    uint8_t  hop_limit;
    uint8_t  src_addr[16];
    uint8_t  dst_addr[16];
} phv_ipv6_t;                /* 40B */

/* ---- TCP 头 ---- */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;    /* data_offset[7:4] reserved[3:0] */
    uint8_t  flags;          /* URG ACK PSH RST SYN FIN */
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} phv_tcp_t;                 /* 20B */

/* ---- UDP 头 ---- */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} phv_udp_t;                 /* 8B */

/* ---- ICMP 头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t rest;
} phv_icmp_t;                /* 8B */

/* ---- MPLS 标签 ---- */
typedef struct __attribute__((packed)) {
    uint32_t label_exp_s_ttl; /* label[31:12] exp[11:9] s[8] ttl[7:0] */
} phv_mpls_label_t;          /* 4B */

/* ---- VXLAN 头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  flags;
    uint8_t  reserved1[3];
    uint8_t  vni[3];
    uint8_t  reserved2;
} phv_vxlan_t;               /* 8B */

/* ---- GRE 头 ---- */
typedef struct __attribute__((packed)) {
    uint16_t flags_ver;
    uint16_t protocol;
    uint32_t key;            /* 可选，flags.K=1时有效 */
} phv_gre_t;                 /* 8B */

/* ---- Geneve 头 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  ver_opt_len;    /* ver[7:6] opt_len[5:0] */
    uint8_t  flags;
    uint16_t protocol;
    uint8_t  vni[3];
    uint8_t  reserved;
} phv_geneve_t;              /* 8B */

/* ---- 元数据（控制信息，不对应报文字段） ---- */
typedef struct __attribute__((packed)) {
    uint16_t ingress_port;   /* 入口端口号 */
    uint16_t egress_port;    /* 出口端口号 */
    uint32_t ingress_ts;     /* 入口时间戳（ns） */
    uint32_t pkt_len;        /* 报文长度（字节） */
    uint16_t pkt_type;       /* 0=单播 1=组播 2=广播 */
    uint16_t drop_reason;    /* 丢包原因码 */
    uint32_t hash_value;     /* 流Hash值 */
    uint32_t color;          /* Meter颜色（0=绿 1=黄 2=红） */
    uint32_t qos_class;      /* QoS类别 */
    uint32_t vrf_id;         /* VRF标识 */
    uint32_t tunnel_id;      /* 隧道标识 */
    uint32_t nexthop_id;     /* 下一跳标识 */
    uint64_t cookie;         /* 用户自定义Cookie */
    uint32_t scratch[4];     /* 暂存寄存器 */
    uint8_t  drop;           /* 丢包标志 */
    uint8_t  mirror;         /* 镜像标志 */
    uint16_t mirror_port;    /* 镜像目的端口 */
    uint16_t mcast_grp_id;   /* 组播组ID */
    uint8_t  encap_type;     /* 封装类型 */
    uint8_t  decap_type;     /* 解封装类型 */
} phv_meta_t;                /* 64B */

/* ================================================================
 * 完整 PHV 结构体（512B）
 * ================================================================ */
typedef struct __attribute__((packed, aligned(64))) {
    /* 偏移 0: 协议头字段区（320B） */
    phv_eth_t         eth;           /* 0-13:   14B */
    phv_vlan_t        vlan;          /* 14-17:   4B */
    phv_qinq_t        qinq;          /* 18-23:   6B */
    phv_ipv4_t        ipv4;          /* 24-43:  20B */
    phv_ipv6_t        ipv6;          /* 44-83:  40B */
    phv_tcp_t         tcp;           /* 84-103: 20B */
    phv_udp_t         udp;           /* 104-111: 8B */
    phv_icmp_t        icmp;          /* 112-119: 8B */
    phv_mpls_label_t  mpls[4];       /* 120-135: 16B */
    phv_vxlan_t       vxlan;         /* 136-143: 8B */
    phv_gre_t         gre;           /* 144-151: 8B */
    phv_geneve_t      geneve;        /* 152-159: 8B */
    uint32_t          user_def[16];  /* 160-223: 64B 用户自定义 */
    uint8_t           int_hdr[32];   /* 224-255: 32B INT遥测头 */

    /* 偏移 256: 元数据区（64B） */
    phv_meta_t        meta;          /* 256-319: 64B */

    /* 偏移 320: 有效标志位图（32B） */
    struct __attribute__((packed)) {
        uint32_t eth_valid      : 1;
        uint32_t vlan_valid     : 1;
        uint32_t qinq_valid     : 1;
        uint32_t ipv4_valid     : 1;
        uint32_t ipv6_valid     : 1;
        uint32_t tcp_valid      : 1;
        uint32_t udp_valid      : 1;
        uint32_t icmp_valid     : 1;
        uint32_t mpls_valid     : 4; /* 每bit对应一层MPLS */
        uint32_t vxlan_valid    : 1;
        uint32_t gre_valid      : 1;
        uint32_t geneve_valid   : 1;
        uint32_t int_valid      : 1;
        uint32_t reserved       : 16;
    } valid_flags;                   /* 320-323: 4B */
    uint8_t  _pad1[28];             /* 324-351: 28B 对齐填充 */

    /* 偏移 352: 保留区（160B，供未来扩展） */
    uint8_t  reserved[160];         /* 352-511: 160B */
} phv_t;                            /* 总计: 512B */

/* 编译时大小检查 */
_Static_assert(sizeof(phv_t) == 512, "PHV size must be 512 bytes");

#endif /* RVP4_PHV_H */
```

## 7.2 phv_meta_t 控制元数据（已含于上方 phv.h）

见 `phv_meta_t` 定义，偏移 256，大小 64B。

## 7.3 tcam_entry_t

见第六章 6.2 节数据结构定义。

## 7.4 action_t

```c
/*
 * action_t - 动作描述符（软件侧，用于HAL API）
 * 硬件侧编码见 3.2.4 Action Word 格式
 */
typedef struct {
    uint8_t  opcode;         /* 操作码（见3.2.4操作码表） */
    uint8_t  dst_field;      /* 目标PHV字段ID */
    uint8_t  src_field_a;    /* 源字段A */
    uint8_t  src_field_b;    /* 源字段B */
    uint16_t imm_value;      /* 立即数 */
    uint16_t meter_id;       /* Meter ID */
    uint16_t counter_id;     /* 计数器ID */
    uint16_t next_table;     /* 下一张表ID */
    uint8_t  flags;          /* 动作标志 */
    uint8_t  reserved[3];
} action_t;                  /* 总大小: 16B */
```

## 7.5 cell 结构（64B，报文存储单元）

```c
/*
 * cell_t - 报文存储单元
 * 报文在TM缓冲区中以64B cell为单位存储
 * 最大帧长9600B = 150 cells
 */
typedef struct __attribute__((packed, aligned(64))) {
    uint8_t  data[56];       /* 报文数据（56B有效载荷） */
    struct {
        uint32_t next_cell   : 20; /* 下一个cell的地址（链表） */
        uint32_t is_last     : 1;  /* 最后一个cell标志 */
        uint32_t byte_valid  : 6;  /* 本cell有效字节数（1-56） */
        uint32_t reserved    : 5;
    } ctrl;                  /* 4B 控制字段 */
    uint32_t pkt_id;         /* 报文ID（用于组播共享） */
} cell_t;                    /* 总大小: 64B */

_Static_assert(sizeof(cell_t) == 64, "cell_t size must be 64 bytes");
```

---

