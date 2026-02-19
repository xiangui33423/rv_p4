## RV-P4 项目文件结构

```
rv_p4/
├── README.md                # 快速上手、测试运行、CLI 命令参考
├── FILES.md                 # 本文件：详细文件索引
├── architecture.md          # 架构概览
├── design_spec.md           # 详细设计规格（2744行）
│
├── XiangShan/               # 香山 RISC-V 核（git submodule）
│
├── rtl/
│   ├── include/
│   │   ├── rv_p4_pkg.sv     # 全局参数、类型、结构体
│   │   └── rv_p4_if.sv      # 所有 Interface 定义
│   │
│   ├── common/
│   │   ├── rst_sync.sv      # 复位同步器（2-FF）
│   │   └── mac_rx_arb.sv    # 32端口 RX 轮询仲裁器
│   │
│   ├── top/
│   │   └── rv_p4_top.sv     # 顶层模块（interface 连接）
│   │
│   ├── parser/
│   │   ├── parser_tcam.sv   # 256×640b 状态转移 TCAM
│   │   └── p4_parser.sv     # Parser 顶层（FSM + PHV 构建）
│   │
│   ├── mau/
│   │   ├── mau_tcam.sv      # 2K×512b TCAM（优先编码）
│   │   ├── mau_alu.sv       # 动作 ALU（SET/ADD/SUB/AND/OR...）
│   │   ├── mau_hash.sv      # Hash 单元（CRC32/CRC16/Jenkins）
│   │   └── mau_stage.sv     # MAU 级顶层（4子级流水）
│   │
│   ├── tm/
│   │   └── traffic_manager.sv  # TM + Deparser（DWRR+SP，256队列）
│   │
│   ├── pkt_buffer/
│   │   └── pkt_buffer.sv    # 包缓冲（1M cell，free list，3端口）
│   │
│   ├── tue/
│   │   └── tue.sv           # 表更新引擎（shadow write + APB）
│   │
│   └── ctrl/
│       └── ctrl_plane.sv    # 控制面（香山核黑盒 + PCIe + APB主控）
│
└── sw/
    ├── hal/
    │   ├── rv_p4_hal.h      # HAL API（TCAM/端口/QoS/Punt/UART）
    │   └── rv_p4_hal.c      # HAL 实现（MMIO → TUE/CSR/UART）
    │
    └── firmware/
        ├── Makefile         # RISC-V ELF 构建 + `make test` 入口
        ├── link.ld          # 裸机链接脚本
        ├── table_map.h      # P4 编译器生成：表/动作 ID 映射
        ├── cp_main.c        # 固件主函数（初始化 + 主循环）
        │
        ├── vlan.h/vlan.c    # VLAN 管理（Access/Trunk，入口/出口 TCAM）
        ├── arp.h/arp.c      # ARP/邻居表（Punt trap + 软件处理 + 老化）
        ├── qos.h/qos.c      # QoS 调度（DSCP 映射，DWRR/SP，PIR 限速）
        ├── fdb.h/fdb.c      # L2 FDB（动态学习/静态条目 + 老化）
        ├── route.h/route.c  # IPv4 LPM 路由（前缀 → 下一跳 TCAM）
        ├── acl.h/acl.c      # ACL 规则（deny/permit，src+dst+dport）
        ├── cli.h/cli.c      # UART CLI 行编辑器（非阻塞轮询）
        ├── cli_cmds.h       # cli_exec_cmd() 接口声明
        └── cli_cmds.c       # CLI 命令实现（show/vlan/arp/route/acl/qos/port/help）
            │
            └── test/        # 单元测试（x86 host，无需 RISC-V 工具链）
                ├── Makefile
                ├── test_framework.h  # TEST_BEGIN/END/ASSERT 宏
                ├── sim_hal.h         # 模拟 HAL 接口（内存 TCAM）
                ├── sim_hal.c         # 模拟 HAL 实现（无 MMIO）
                ├── test_main.c       # 测试套件入口（37 个用例）
                ├── test_vlan.c       # VLAN 测试（6 个）
                ├── test_arp.c        # ARP 测试（7 个）
                ├── test_qos.c        # QoS 测试（5 个）
                ├── test_route.c      # 路由测试（3 个）
                ├── test_acl.c        # ACL 测试（4 个）
                ├── test_cli.c        # CLI 测试（6 个）
                └── test_integration.c # 集成/系统测试（6 个）
```
