# RV-P4 — RISC-V + P4 可编程交换机

RV-P4 是一个面向数据中心的高性能可编程交换机原型，由两部分组成：

- **RTL 数据平面**：基于 PISA 流水线（24 MAU 级，2K×512b TCAM/级，256 队列 TM）
- **控制面固件**：运行在片上香山 RISC-V 64-bit 核，通过 MMIO/APB 驱动 HAL 操作数据平面

---

## 目录结构

```
rv_p4/
├── README.md               ← 本文件
├── FILES.md                ← 文件索引（详细）
├── architecture.md         ← 架构概览
├── design_spec.md          ← 详细设计规格
│
├── XiangShan/              ← 香山核子模块（git submodule）
│
├── rtl/                    ← SystemVerilog 数据平面 RTL
│   ├── include/            rv_p4_pkg.sv, rv_p4_if.sv
│   ├── parser/             p4_parser.sv, parser_tcam.sv
│   ├── mau/                mau_stage.sv, mau_tcam.sv, mau_alu.sv, mau_hash.sv
│   ├── tm/                 traffic_manager.sv
│   ├── pkt_buffer/         pkt_buffer.sv
│   ├── tue/                tue.sv
│   ├── common/             rst_sync.sv, mac_rx_arb.sv
│   └── top/                rv_p4_top.sv
│
└── sw/                     ← 控制面软件
    ├── hal/
    │   ├── rv_p4_hal.h     HAL API（TCAM/端口/QoS/Punt/UART）
    │   └── rv_p4_hal.c     HAL 实现（MMIO → TUE/CSR）
    │
    └── firmware/
        ├── Makefile        RISC-V ELF 构建 + 测试入口
        ├── link.ld         链接脚本
        ├── table_map.h     P4 编译器生成：表/动作 ID 映射
        ├── cp_main.c       固件主函数（初始化 + 主循环）
        │
        ├── vlan.c/h        VLAN 管理（Access/Trunk，入口/出口 TCAM）
        ├── arp.c/h         ARP/邻居表（Punt trap + 软件处理 + 老化）
        ├── qos.c/h         QoS 调度（DSCP 映射，DWRR/SP，PIR 限速）
        ├── fdb.c/h         L2 FDB（动态学习/静态 + 老化）
        ├── route.c/h       IPv4 LPM 路由（前缀 → 下一跳 TCAM）
        ├── acl.c/h         ACL 规则（deny/permit，src+dst+dport）
        ├── cli.c/h         UART CLI 行编辑器（非阻塞轮询）
        ├── cli_cmds.c/h    CLI 命令实现（8 大命令族）
        │
        └── test/           ← 单元测试（x86 host，无需 RISC-V 工具链）
            ├── Makefile
            ├── test_framework.h  TEST_BEGIN/TEST_END/TEST_ASSERT 宏
            ├── sim_hal.h/c       模拟 HAL（内存 TCAM，无 MMIO）
            ├── test_main.c         测试套件入口（37 个用例）
            ├── test_vlan.c         VLAN 测试（6 个）
            ├── test_arp.c          ARP 测试（7 个）
            ├── test_qos.c          QoS 测试（5 个）
            ├── test_route.c        路由测试（3 个）
            ├── test_acl.c          ACL 测试（4 个）
            ├── test_cli.c          CLI 测试（6 个）
            └── test_integration.c  集成/系统测试（6 个）
```

---

## 快速上手：运行单元测试

### 前提条件

只需要标准的 x86 `gcc`，**不需要** RISC-V 工具链。

```bash
gcc --version   # >= 7.0 即可
```

### 方法一：在 firmware 目录下运行

```bash
cd sw/firmware
make test
```

### 方法二：直接进入 test 目录运行

```bash
cd sw/firmware/test
make
```

### 预期输出

```
RV-P4 Control Plane Unit Tests
================================
[SUITE] VLAN Management (6 cases)
  PASS  VLAN-1 : vlan_create installs ingress TCAM entry
  PASS  VLAN-2 : vlan_delete removes ingress TCAM entry
  PASS  VLAN-3 : access port ingress — PVID rewrite action
  PASS  VLAN-4 : trunk port ingress — pass-through action
  PASS  VLAN-5 : vlan_port_add installs egress TCAM entry
  PASS  VLAN-6 : vlan_port_remove deletes egress TCAM entry

[SUITE] ARP / Neighbor Table (7 cases)
  PASS  ARP-1  : arp_init installs punt-trap TCAM rule
  PASS  ARP-2  : arp_add triggers fdb_learn (TCAM check)
  PASS  ARP-3  : arp_lookup miss returns HAL_ERR
  PASS  ARP-4  : arp_delete removes entry
  PASS  ARP-5  : process ARP request → send reply
  PASS  ARP-6  : process ARP reply → learn FDB
  PASS  ARP-7  : dynamic entries age out after timeout

[SUITE] QoS Scheduling (5 cases)
  PASS  QOS-1  : default DSCP→TC map covers TC0..TC5
  PASS  QOS-2  : dscp_init installs TCAM rules for all 64 codepoints
  PASS  QOS-3  : DSCP rule TCAM encoding correct
  PASS  QOS-4  : DWRR weight registers written correctly
  PASS  QOS-5  : PIR shaper + scheduler mode set

[SUITE] IPv4 Routing (3 cases)
  PASS  ROUTE-1: route_add installs TCAM; route_del removes it
  PASS  ROUTE-2: /32 host route — exact-match mask
  PASS  ROUTE-3: 0.0.0.0/0 default route; len=33 returns error

[SUITE] ACL Rules (4 cases)
  PASS  ACL-1  : acl_add_deny → ACTION_DENY with correct key
  PASS  ACL-2  : acl_add_permit → ACTION_PERMIT, key_len=8
  PASS  ACL-3  : acl_delete removes entry; bad ID returns error
  PASS  ACL-4  : sequential rule IDs; mid-delete leaves others intact

[SUITE] CLI Commands (6 cases)
  PASS  CLI-1  : unknown command returns 0
  PASS  CLI-2  : help command recognized (returns 1)
  PASS  CLI-3  : 'route add 10.0.0.0/8 2 aa:bb:cc:dd:ee:ff' installs TCAM
  PASS  CLI-4  : 'route del 10.0.0.0/8' removes TCAM entry
  PASS  CLI-5  : 'acl deny 192.168.0.0/16 0.0.0.0/0 80' installs ACTION_DENY
  PASS  CLI-6  : 'vlan create 50' + 'vlan port 50 add 3 untagged' → egress TCAM

[SUITE] Integration / System (6 cases)
  PASS  SYS-1  : 全量初始化 — 各 Stage 条目数正确，无 TCAM 溢出
  PASS  SYS-2  : ARP Request Punt → Reply 内容 + ARP表 + FDB TCAM 联动
  PASS  SYS-3  : ARP Reply Punt → ARP表与 FDB TCAM 字段双向一致
  PASS  SYS-4  : arp_delete 后 FDB TCAM 残留（已知缺陷，当前行为断言）
  PASS  SYS-5  : Route/ACL/FDB 写入各自 Stage，互不干扰
  PASS  SYS-6  : CLI 序列(route+acl+vlan) → 多 Stage TCAM 同时生效

================================
Results: 37/37 passed  ✓ ALL PASS
================================
```

## 测试套件说明

测试分为两层：

| 套件 | 文件 | 用例数 | 说明 |
|------|------|--------|------|
| VLAN Management | `test_vlan.c` | 6 | 单模块，TCAM 规则安装/删除 |
| ARP / Neighbor | `test_arp.c` | 7 | 单模块，Punt 收包/老化 |
| QoS Scheduling | `test_qos.c` | 5 | 单模块，DSCP/DWRR/PIR |
| IPv4 Routing | `test_route.c` | 3 | 单模块，LPM TCAM |
| ACL Rules | `test_acl.c` | 4 | 单模块，deny/permit/del |
| CLI Commands | `test_cli.c` | 6 | 单模块，命令解析到 TCAM |
| **Integration / System** | **`test_integration.c`** | **6** | **跨模块端到端流程** |

集成测试覆盖的跨模块场景：

- **SYS-1**：所有模块同时初始化，验证 7 个 Stage 的 TCAM 条目数与总量
- **SYS-2**：ARP Request 以太帧 → `arp_process_pkt` → Reply 内容逐字节验证 + ARP 表 + FDB TCAM 三路联动
- **SYS-3**：ARP Reply 处理后 ARP 软件表与 FDB TCAM 的 port 字段双向一致性
- **SYS-4**：已知缺陷记录 — `arp_delete` 未联动调用 `fdb_delete`，FDB TCAM 条目残留（断言当前实际行为）
- **SYS-5**：Route(Stage 0) + ACL(Stage 1) + FDB(Stage 2) 三模块共存，各 Stage 严格隔离
- **SYS-6**：CLI 多命令序列 → Stage 0/1/6 同时写入，验证无交叉污染

---



```bash
cd sw/firmware/test
make clean
# 或从 firmware 目录：
make clean   # 同时清理 test/
```

---

## 构建固件 ELF（需要 RISC-V 工具链）

```bash
# 安装工具链（Ubuntu/Debian）
sudo apt install gcc-riscv64-unknown-elf

# 构建
cd sw/firmware
make

# 产物
ls cp_firmware.elf
```

编译标志：`-march=rv64gc -mabi=lp64d -O2 -ffreestanding -nostdlib`

### x86 仿真构建（手工调试，无需 RISC-V 工具链）

```bash
cd sw/firmware
make sim
./cp_firmware_sim
```

---

## MMIO 地址映射

| 基地址       | 模块           | 说明                        |
|------------|----------------|-----------------------------|
| 0xA0000000 | Parser CSR     | PHV 字段提取配置              |
| 0xA0001000 | MAU / TUE      | TCAM 表更新引擎              |
| 0xA0002000 | TM             | 流量管理器（调度/整形）        |
| 0xA0003000 | TUE APB        | TCAM shadow write 接口       |
| 0xA0004000 | Pkt Buffer     | 包缓冲控制                   |
| 0xA0005000 | VLAN CSR       | PVID/端口模式寄存器           |
| 0xA0006000 | QoS CSR        | DWRR 权重/PIR 寄存器         |
| 0xA0007000 | Punt FIFO      | CPU Punt RX/TX FIFO         |
| 0xA0009000 | UART           | 控制台（CLI 输入/输出）        |

---

## PISA 流水线 Stage 分配

| Stage | 用途             | 软件模块      |
|-------|-----------------|---------------|
| 0     | IPv4 LPM 路由   | `route.c`     |
| 1     | ACL 入口过滤    | `acl.c`       |
| 2     | L2 FDB 转发     | `fdb.c`       |
| 3     | ARP Punt Trap   | `arp.c`       |
| 4     | VLAN 入口处理   | `vlan.c`      |
| 5     | DSCP → TC 映射  | `qos.c`       |
| 6     | VLAN 出口处理   | `vlan.c`      |

---

## CLI 命令参考

固件启动后通过 UART（115200-8N1）连接，提示符为 `rv-p4> `。

### show

```
show vlan                      # 列出所有 VLAN
show arp                       # 显示 ARP/邻居表
show route                     # 显示 IPv4 路由表
show fdb                       # 显示 L2 FDB 表
show port <port>               # 显示端口统计
show qos <port>                # 显示 QoS 配置
show acl                       # 显示 ACL 规则
```

### vlan

```
vlan create <vid>
vlan delete <vid>
vlan port <vid> add <port> tagged|untagged
vlan port <vid> del <port>
vlan pvid <port> <vid>
```

### route

```
route add <prefix/len> <port> <nexthop-mac>
  # 示例: route add 10.0.0.0/8 2 aa:bb:cc:dd:ee:ff
route del <prefix/len>
```

### acl

```
acl deny   <src-prefix/len> <dst-prefix/len> <dport>
acl permit <src-prefix/len> <dst-prefix/len> <dport>
acl del    <rule-id>
  # 示例: acl deny 192.168.0.0/16 0.0.0.0/0 80
```

### arp

```
arp add   <ip> <port> <mac>
arp del   <ip>
arp probe <ip>
```

### qos

```
qos weight <port> <w0> <w1> <w2> <w3> <w4> <w5> <w6> <w7>
qos pir    <port> <bps>
qos mode   <port> sp|dwrr|sp-dwrr <sp-queues>
qos dscp   <dscp> <tc>
```

### port

```
port enable  <port>
port disable <port>
port stats   <port>
```

---

## 模块依赖关系

```
cp_main.c
  ├── hal/rv_p4_hal.{h,c}   ← MMIO 驱动（底层）
  ├── table_map.h            ← P4 编译器常量
  ├── vlan.{h,c}
  ├── arp.{h,c}              → depends on fdb.h (fdb_learn)
  ├── qos.{h,c}
  ├── fdb.{h,c}
  ├── route.{h,c}
  ├── acl.{h,c}
  ├── cli.{h,c}              ← UART 行编辑，调用 cli_exec_cmd
  └── cli_cmds.{h,c}         ← 命令解析，调用上述所有模块
```

---

## 许可证

本项目仅供学术研究与教学使用。
