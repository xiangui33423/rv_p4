## RV-P4 项目文件结构

```
rv_p4/
├── README.md                # 快速上手、测试运行、CLI 命令参考、RTL 联合仿真
├── FILES.md                 # 本文件：详细文件索引
├── architecture.md          # 架构概览（时钟域、数据流、接口定义）
├── design_spec.md           # 详细设计规格
│
├── XiangShan/               # 香山 RISC-V 核（git submodule）
│
├── rtl/
│   ├── include/
│   │   ├── rv_p4_pkg.sv     # 全局参数、类型、结构体
│   │   └── rv_p4_if.sv      # 所有 Interface 定义
│   │
│   ├── common/
│   │   ├── rst_sync.sv      # 复位同步器（2-FF，异步复位同步释放）
│   │   └── mac_rx_arb.sv    # 32端口 RX 轮询仲裁器（S_IDLE/S_GRANT FSM）
│   │
│   ├── top/
│   │   ├── rv_p4_top.sv     # 顶层模块（interface 连接，含 tb_tue_* 背门）
│   │   └── ctrl_plane.sv    # 控制面（香山核黑盒 + PCIe，含 tb_tue_* APB 背门）
│   │
│   ├── parser/
│   │   ├── parser_tcam.sv   # 256×640b 状态转移 TCAM（XNOR+OR 匹配）
│   │   └── p4_parser.sv     # Parser 顶层（FSM + PHV 逐字节提取）
│   │
│   ├── mau/
│   │   ├── mau_tcam.sv      # 2K×512b TCAM（优先编码，mask=1→don't care）
│   │   ├── mau_alu.sv       # 动作 ALU（imm_val=action_params[47:16]）
│   │   ├── mau_hash.sv      # Hash 单元（CRC32/CRC16/Jenkins）
│   │   └── mau_stage.sv     # MAU 级顶层（4子级流水：crossbar→TCAM→ASRAM→ALU）
│   │
│   ├── tm/
│   │   └── traffic_manager.sv  # TM（DWRR+SP，256队列，读 pkt_buffer，驱动 MAC TX）
│   │
│   ├── pkt_buffer/
│   │   └── pkt_buffer.sv    # 包缓冲（1M cell×64B，free list，3读1写端口）
│   │
│   ├── tue/
│   │   └── tue.sv           # 表更新引擎（shadow write + 36周期事务，ASRAM写入）
│   │
│   └── deparser/
│       └── deparser.sv      # Deparser（PHV → 出口报文重组）
│
├── tb/
│   └── cosim/               # RTL + 固件 Verilator 联合仿真
│       ├── Makefile         # 构建：verilate RTL + 编译固件 + 链接 cosim_sim
│       └── cosim_main.cpp   # 仿真驱动：时钟管理、APB 写、Parser 编程、报文注入
│
└── sw/
    ├── hal/
    │   ├── rv_p4_hal.h      # HAL API（TCAM/端口/QoS/Punt/UART）
    │   └── rv_p4_hal.c      # HAL 实现（MMIO → TUE/CSR/UART）
    │
    └── firmware/
        ├── Makefile         # RISC-V ELF 构建 + `make test` 入口
        ├── link.ld          # 裸机链接脚本
        ├── table_map.h      # P4 编译器生成：表/动作 ID 映射、PHV 字节偏移
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
                ├── pkt_model.h       # PISA 数据面功能模型接口
                ├── pkt_model.c       # PISA 数据面功能模型实现
                ├── test_main.c       # 测试套件入口（44 个用例）
                ├── test_vlan.c       # VLAN 测试（6 个）
                ├── test_arp.c        # ARP 测试（7 个）
                ├── test_qos.c        # QoS 测试（5 个）
                ├── test_route.c      # 路由测试（3 个）
                ├── test_acl.c        # ACL 测试（4 个）
                ├── test_cli.c        # CLI 测试（6 个）
                ├── test_integration.c # 集成/系统测试（6 个）
                └── test_dp_cosim.c   # 软件数据面联合测试（7 个）
```
