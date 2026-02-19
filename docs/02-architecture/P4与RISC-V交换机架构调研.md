# **深度研究报告：通用P4交换机架构与基于RISC-V的异构数据平面实现机制**

## **1\. 引言**

随着软件定义网络（SDN）的深入发展，网络数据平面的可编程性已成为下一代网络基础设施的核心需求。传统的固定功能ASIC（专用集成电路）虽然在性能上达到了极高的吞吐量，但在协议演进和定制化业务逻辑的支持上显得僵化且周期漫长。P4（Programming Protocol-independent Packet Processors）语言的出现，标志着数据平面从“配置”向“编程”的范式转变。P4允许网络架构师通过声明式语言定义数据包的解析、处理和转发逻辑，从而在不改变硬件的前提下支持新协议。

然而，通用的P4架构（如PISA，Protocol Independent Switch Architecture）主要面向线速的报头处理，其设计哲学在确定性延迟和指令复杂度之间进行了严格的权衡，导致其在处理复杂状态机、浮点运算、加密解密以及复杂的应用层逻辑时存在先天局限。为了突破这一瓶颈，学术界和工业界开始探索将通用计算核心——特别是基于RISC-V指令集架构（ISA）的处理器——集成到P4可编程数据平面中。RISC-V凭借其开源、模块化和可扩展的特性，成为实现“网络内计算”（In-Network Computing, INC）的理想载体。

本报告旨在详尽调研通用的P4交换机架构，并在此基础上深入剖析基于RISC-V的P4交换机实现方式。报告将从底层的流水线设计、内存层次结构、指令集扩展、编译器工具链以及具体的架构图文献（如PsPIN、FPsPIN、OmniXtend等）等多个维度进行全方位的技术拆解。

## ---

**2\. 通用P4交换机架构体系详解**

在深入探讨RISC-V集成之前，必须透彻理解P4交换机的基准架构。当前，P4生态系统中最具代表性的架构模型是协议无关交换机架构（PISA）及其标准化描述——便携式交换机架构（PSA）。

### **2.1 协议无关交换机架构（PISA）与RMT模型**

PISA架构的核心思想是将数据包的处理抽象为一条由可编程模块构成的流水线。其硬件实现的理论基础主要源自可重构匹配表（Reconfigurable Match Tables, RMT）模型。RMT模型通过解耦“匹配”与“动作”的资源绑定，利用共享的SRAM/TCAM资源池和交叉开关（Crossbar）互联，实现了对任意协议字段的灵活处理1。

#### **2.1.1 可编程解析器（Programmable Parser）**

PISA流水线的起点是可编程解析器。与传统交换机仅能识别Ethernet、IP、TCP等固定头部不同，P4解析器允许用户定义一个有限状态机（FSM）2。

* **架构机制**：硬件上，解析器通常由TCAM（三态内容寻址存储器）或专用的解析逻辑单元实现。输入的数据流被切分为固定宽度的字，状态机根据当前状态和输入数据决定下一个状态，并提取相应的字段填充到数据包头向量（Packet Header Vector, PHV）中。  
* **PHV的作用**：PHV是贯穿整个流水线的核心总线，承载了所有提取的报头字段和中间元数据。其宽度决定了交换机单次能处理的元数据总量。  
* **局限性**：尽管灵活，但为了保证线速（Line-rate），解析器通常不支持回溯，且循环解析（如处理MPLS标签栈）的深度受限于硬件时钟周期4。

#### **2.1.2 匹配-动作流水线（Match-Action Pipeline）**

这是PISA架构的心脏，由多个顺序排列的匹配-动作单元（MAU）组成。

* **匹配阶段（Match）**：每个阶段包含多个逻辑表。这些表通过Crossbar从PHV中选择字段作为查找键（Key）。查找内存通常分为精确匹配（Exact Match，使用SRAM/Hash）和三态匹配（Ternary Match，使用TCAM）。RMT架构允许不同阶段动态共享这些存储资源1。  
* **动作阶段（Action）**：匹配成功后，输出动作指令和参数。动作引擎通常由超长指令字（VLIW）处理器或并行ALU阵列构成，能够在一个时钟周期内对PHV中的多个字段执行加减、移位、哈希计算或封装/解封装操作5。  
* **元数据传递**：除了报头，流水线还传递用户定义的元数据（User-defined Metadata）和标准元数据（Standard Metadata，如入端口索引、时间戳），这些数据在各阶段间流动，用于控制逻辑判断2。

#### **2.1.3 流量管理器（Traffic Manager, TM）**

流量管理器通常是一个固定功能的硬件块，位于入口流水线（Ingress）和出口流水线（Egress）之间。它负责：

* **缓冲（Buffering）**：在拥塞时存储数据包。  
* **调度（Scheduling）**：根据QoS策略（如加权轮询WRR、优先级队列）选择下一个发送的数据包。  
* **复制（Replication）**：通过多播引擎将单播包复制到多个出口端口或镜像到CPU2。  
* **队列管理**：执行丢包策略（如WRED、Tail Drop）。

#### **2.1.4 可编程重组器（Deparser）**

流水线的末端是重组器。它根据P4程序中定义的顺序，将PHV中经过修改的字段重新序列化为字节流，准备发送到物理链路。重组器的灵活性允许交换机在出口处任意添加、删除或重写报头（例如在VxLAN封装中添加外层IP头）2。

### **2.2 便携式交换机架构（PSA）规范**

为了解决P4程序在不同硬件目标（Target）间的移植性问题，P4.org工作组定义了PSA规范。PSA详细规定了标准的可编程块接口和外部函数（Externs）2。

| PSA 模块组件 | 功能描述 | 硬件映射特征 |
| :---- | :---- | :---- |
| **IngressParser** | 解析入站数据包，提取Header到PHV | TCAM/状态机逻辑 |
| **Ingress Pipeline** | 执行转发决策、ACL、流控、INT遥测插入 | RMT Pipeline / ALU Array |
| **IngressDeparser** | 在进入TM前重组数据包（主要用于克隆或再循环） | 序列化逻辑 |
| **PRE (Packet Replication Engine)** | 处理多播和克隆（Clone）操作 | 专用的复制逻辑电路 |
| **BQE (Buffer Queuing Engine)** | 队列调度与拥塞管理 | 固定功能调度器 |
| **EgressParser** | 解析出站数据包（处理多播复制后的副本） | 简化的解析逻辑 |
| **Egress Pipeline** | 执行出站修改（如VLAN Tagging、TTL递减） | RMT Pipeline / ALU Array |
| **EgressDeparser** | 最终序列化 | 序列化逻辑 |

PSA架构引入了严格的类型系统，区分了packet\_in（解析器输入）、packet\_out（重组器输出）以及各种元数据结构。这种标准化使得开发者可以专注于逻辑实现，而无需过分关注底层是基于Tofino ASIC还是基于FPGA的实现，前提是编译器后端支持该映射。

### **2.3 通用架构的局限性与演进需求**

尽管PISA/PSA架构在二层/三层转发上表现卓越，但在面对复杂的应用层业务时显露出明显的不足：

1. **状态存储限制**：P4的寄存器（Register）虽然支持有状态处理，但其容量有限（通常为SRAM），且不支持复杂的指针操作或动态内存分配，难以实现大容量的流表或缓存。  
2. **计算复杂度限制**：ALU通常只支持简单的整数运算和位操作。浮点运算、乘除法、复杂的加密算法（如AES-GCM）、正则表达式匹配（Regex）等在纯P4流水线中难以实现或资源消耗极高。  
3. **循环与迭代限制**：为了保证确定的时延，P4流水线通常不支持未绑定的循环（Unbounded Loops），这限制了对变长载荷的深度处理能力。

这些局限性直接催生了将通用计算核心（CPU）引入数据平面的需求。而在众多CPU架构中，RISC-V凭借其开放性、模块化和可定制指令集的特性，成为了与P4结合的首选。

## ---

**3\. 基于RISC-V的P4异构架构设计原理**

将RISC-V集成到P4交换机中，本质上是构建一种异构计算架构：P4流水线负责“快路径”（Fast Path），处理高吞吐、低复杂度的转发逻辑；RISC-V核心集群负责“智能路径”（Smart Path），处理低吞吐但高复杂度的计算任务。

### **3.1 架构集成的三种模式**

根据RISC-V核心与P4流水线的耦合程度，可以归纳为三种主要的架构模式：

#### **3.1.1 旁路控制模式（Control Plane Integration）**

这是最传统的模式，RISC-V作为交换机的控制平面CPU（管理处理器）。

* **工作流**：P4流水线通过packet\_in将无法识别或需要特殊处理的报文发送给CPU。RISC-V CPU运行Linux操作系统和控制面软件（如FRRouting），处理完毕后通过packet\_out注入回流水线或更新P4匹配表项6。  
* **性能特征**：高延迟（毫秒级），低吞吐。主要用于路由协议交互、异常处理，而非数据平面业务。

#### **3.1.2 旁挂加速模式（Look-aside Acceleration）**

RISC-V核心作为协处理器挂载在交换芯片旁侧。

* **工作流**：P4流水线识别特定流量（如加密流量），将其导向（Steer）连接到RISC-V引擎的端口。RISC-V处理后将报文返还给交换机。  
* **架构实例**：SmartNIC中常采用此模式，利用PCIe或内部总线连接P4逻辑与计算核心。

#### **3.1.3 在线流处理模式（Inline/On-path Processing）**

这是“网络内计算”的终极形态，也是当前研究的热点（如PsPIN架构）。

* **工作流**：RISC-V核心集群直接嵌入在流水线的中间或末端，共享片上高带宽内存。数据包在解析后直接进入RISC-V核心的紧耦合内存（TCDM），核心以流水线方式处理报文载荷，处理完后直接送往出口。  
* **性能特征**：低延迟（微秒级），高吞吐。RISC-V核心通常采用裸机（Bare-metal）运行，无操作系统开销。

### **3.2 为什么选择RISC-V？**

1. **指令集扩展性（ISA Extensions）**：  
   P4处理涉及大量的位字段操作（Bit manipulation）、包头提取和校验和计算。标准的x86或ARM指令集在处理这些非字节对齐数据时效率较低。RISC-V允许设计者添加自定义指令（Custom Extensions）。  
   * **Zbb扩展**：提供位操作指令（如clz, ctz, pcnt），加速报头解析8。  
   * **向量扩展（RVV）**：支持SIMD操作，可并行处理多个数据包或执行高吞吐量的加密/压缩算法10。  
   * **自定义包处理指令**：研究文献12提到，可以通过定义新的R-type指令直接控制硬件加速器（如CAN-FD控制器或DMA引擎），消除了传统内存映射I/O（MMIO）的总线开销。  
2. **开源与IP核灵活性**： P4交换机设计往往是高度定制的SoC。RISC-V的开源IP（如PULP, Snitch, Rocket, Ibex）允许芯片设计者根据流水线的时序和面积约束，裁剪CPU核的特性（如去除FPU、调整缓存大小、增加TCDM接口），这在ARM或x86生态中是不可想象的13。

## ---

**4\. 深入调研：基于RISC-V的P4交换机实现与架构文献**

本章节将深入剖析具体的实现案例和文献中提出的架构图细节。

### **4.1 PsPIN：基于PULP集群的在线流处理架构**

**PsPIN**（Programmable sPIN）是苏黎世联邦理工学院（ETH Zürich）提出的一种基于RISC-V的高性能包处理架构，它实现了sPIN编程模型。这是目前文献中描述最详尽的P4+RISC-V紧耦合架构之一12。

#### **4.1.1 顶层架构设计**

文献14中的架构图展示了PsPIN作为一个独立的IP模块集成在NIC或交换机流水线中。其核心组件包括：

* **L2 报文缓冲区（L2 Packet Buffer）**：  
  这是一个共享的4 MiB SRAM存储器，用于暂存进入PsPIN的数据包载荷。其访问延迟约为15-50个周期。所有进入的数据包首先由DMA写入此缓冲区。  
* **分层计算集群（Compute Clusters）**：  
  PsPIN采用了分层多核设计。原型包含**4个计算集群**，每个集群内部包含**8个RISC-V核心**（共32核）。  
  * **核心类型**：采用**RI5CY**或**Snitch**核心。Snitch是一种极简的伪双发射核心，针对面积效率进行了优化，非常适合大规模并行集成。  
  * **L1 TCDM（紧耦合数据内存）**：这是架构的关键。每个集群拥有自己的L1暂存器（Scratchpad Memory），支持单周期访问。L2中的数据包片段会被DMA预取到L1 TCDM中，供RISC-V核心以极低延迟处理。这种设计避免了多核争抢L2带宽造成的阻塞。  
* **硬件调度器（Hardware Scheduler）**：  
  为了保证线速处理，PsPIN引入了专门的硬件调度单元。它负责解析P4流水线发来的处理请求（Handler Requests），并将任务分发给空闲的RISC-V核心。调度器还负责维护处理顺序，确保同一数据流（Flow）的数据包按序输出。

#### **4.1.2 信号与数据流**

1. **P4 Ingress**：P4程序解析报头，匹配表决定该包需要PsPIN处理，生成一个描述符（包含处理函数ID、包长度、内存偏移）。  
2. **DMA Copy**：入站引擎（Inbound Engine）将报文载荷从线路接口直接DMA写入L2 Buffer，同时通知调度器。  
3. **Dispatch**：调度器唤醒一个RISC-V核心，核心利用集群内的DMA将感兴趣的报文片段从L2拉取到L1 TCDM。  
4. **Execution**：RISC-V核心执行用户编写的C/C++处理函数（Handler）。指令集包含特定扩展，用于快速访问报头字段。  
5. **Egress**：处理完成后，核心通过出站引擎（Outbound Engine）将修改后的数据包发送回网络或主机内存。

#### **4.1.3 Snitch核心与ISA扩展**

PsPIN中的Snitch核心扩展了**SSR（Stream Semantic Registers）**。这是一种微架构优化，允许核心将加载/存储指令隐式编码为寄存器读写。例如，当核心连续读取一个数据流时，SSR会自动预取数据到寄存器，CPU只需执行算术指令，从而将数据搬运与计算重叠，极大提升了流水线效率18。

### **4.2 FPsPIN：FPGA上的异构原型实现**

**FPsPIN**是PsPIN架构在FPGA上的完整实现，旨在验证硬件可行性并提供开源研究平台19。

#### **4.2.1 硬件平台与集成**

* **宿主平台**：基于**NetFPGA-SUME**或类似的高性能FPGA板卡（如Xilinx UltraScale+）。  
* **基础NIC架构**：FPsPIN构建在**Corundum**开源NIC之上。Corundum提供了基本的MAC、DMA和PCIe接口。FPsPIN逻辑被封装在Corundum的“App Block”区域中。  
* **时钟域跨越**：架构图中特别展示了异步FIFO的使用。Corundum的数据路径运行在250 MHz（以支持100Gbps），而由于FPGA上的软核RISC-V时序收敛困难，RISC-V集群仅运行在40 MHz。这种频率失配是FPGA原型的主要瓶颈，但在ASIC实现中可以消除。

#### **4.2.2 内存映射与互联**

FPsPIN的架构图展示了复杂的AXI互联结构：

* **AXI-Stream**：用于数据包在Corundum数据路径和PsPIN入站/出站引擎之间的高速传输。  
* **AXI-Lite**：用于主机CPU配置PsPIN的控制寄存器（如加载RISC-V程序二进制代码）。  
* **AXI-Full**：用于RISC-V核心通过片外DMA引擎访问主机DRAM（Host Memory）。这使得FPsPIN不仅能处理网络包，还能直接操作主机内存中的数据结构（如键值存储表），这是传统P4无法做到的。

### **4.3 ControlPULP：面向控制平面的RISC-V集成**

除了数据平面加速，RISC-V也被用于增强控制平面的响应速度。**ControlPULP**架构7展示了如何将并行RISC-V集群用于电源管理和复杂的QoS控制。

* **架构概览**：ControlPULP包含一个管理核心（Manager Core, CV32E40P）和一个包含8个工作核心（Worker Cores）的集群。  
* **与P4的交互**：在网络场景中，P4交换机可以将遥测数据（Telemetry Data）镜像到ControlPULP。RISC-V集群并行分析这些数据（如计算全网拥塞状态），并实时调整P4流表中的带宽限制参数或路由权重。这种闭环控制的时延比通过PCIe上传到x86服务器处理要低几个数量级。

### **4.4 OmniXtend：P4交换机作为RISC-V的缓存一致性互联**

这是一个独特的反向视角：不是在交换机里放RISC-V，而是用P4交换机来连接RISC-V CPU，构建大规模NUMA系统22。

#### **4.4.1 架构图解析**

OmniXtend架构图24展示了一个以P4交换机（如Tofino）为中心的星型拓扑：

* **节点（Nodes）**：多个RISC-V SoC节点，每个节点包含L1/L2缓存和TileLink总线接口。  
* **转换层**：节点输出的TileLink一致性消息被封装在以太网帧中（TLoE, TileLink over Ethernet）。  
* **P4交换机逻辑**：  
  * **解析器**：识别TLoE自定义以太网类型（EtherType）。  
  * **Match-Action表**：维护一个全局的目录（Directory），记录每个缓存行（Cache Line）的状态（MESI协议状态）。  
  * **转发逻辑**：当一个节点请求写权限（AcquirePerm）时，P4交换机查找目录，向持有该缓存行副本的其他节点发送失效探测（Probe），并在收集确权后回复请求节点。  
* **意义**：该架构证明了P4流水线的可编程性足以处理复杂的缓存一致性协议，使得基于RISC-V的高性能计算集群可以通过廉价的商用以太网交换机进行扩展，而无需昂贵的专有互联芯片。

### **4.5 商业化实现：Pensando与BlueField**

工业界的实现虽然部分闭源，但其架构图在技术白皮书中有所披露，验证了RISC-V+P4的技术路线。

#### **4.5.1 AMD Pensando (DSC)**

Pensando的数据处理单元（DPU）采用了一种“P4原生”的架构26。

* **MPU（Match Processing Unit）**：Pensando没有将P4逻辑硬化为ASIC门电路，而是设计了高度定制的处理器（MPU）来执行P4动作。  
* **流水线组织**：架构图显示了一系列串联的Stage。每个Stage包含一个**Table Engine（TE）和一个MPU集群**。  
  * **TE**：负责提取键值，查找SRAM/TCAM，将结果投递给MPU。  
  * **MPU**：执行ALU操作。这些MPU本质上是精简指令集的处理器（类RISC-V），专门优化了位操作和数据包字段访问。  
* **优势**：这种软件定义流水线比纯ASIC更灵活，比通用CPU更高效。

#### **4.5.2 NVIDIA BlueField**

BlueField-3引入了**DPA（Data Path Accelerator）**，这是一个包含16个RISC-V核心的子系统28。

* **位置**：DPA位于ConnectX网卡逻辑和ARM主控CPU之间。  
* **DOCA库**：用户通过DOCA P4接口定义流水线。对于复杂的有状态处理（如连接跟踪、复杂的负载均衡算法），编译器会将其卸载到DPA的RISC-V核心上执行。架构图显示DPA拥有独立的L1/L2缓存，并通过高带宽环形总线与网卡的数据缓冲区相连。

## ---

**5\. 编译器与软件工具链支持**

硬件架构的成功离不开编译器的支持。P4C（P4 Compiler）生态系统正在积极扩展对RISC-V后端的支持。

### **5.1 P4到RISC-V的编译流程**

要将P4程序运行在RISC-V核心上，通常经历以下流程30：

1. **Frontend（前端）**：P4C前端解析P4-16代码，生成P4 IR（中间表示）。  
2. **Midend（中端）**：执行目标无关的优化，如死代码消除、表项聚合。  
3. **Backend（后端）**：  
   * **C/C++ Transpilation**：对于像PsPIN这样的软核目标，后端通常将P4 Action转换为优化的C++代码或eBPF字节码。  
   * **LLVM Integration**：生成的C代码通过RISC-V优化的LLVM/Clang编译器生成最终的二进制机器码32。  
4. **Extern Mapping**：P4中的extern函数（如Checksum、Hash）被映射为RISC-V的特定指令序列或对硬件加速器的内联汇编调用。

### **5.2 虚拟化与eBPF支持**

在RISC-V Linux环境下（如旁路控制模式），P4程序可以被编译为eBPF（Extended Berkeley Packet Filter）字节码，并挂载到内核的XDP（eXpress Data Path）钩子上。RISC-V架构对eBPF JIT（即时编译）的支持正在日益完善，使得RISC-V核心能高效执行数据包过滤逻辑30。

## ---

**6\. 性能考量与未来展望**

### **6.1 性能与资源权衡（PPA）**

* **延迟确定性**：P4 ASIC提供纳秒级的确定性延迟。引入RISC-V后，由于指令缓存缺失（I-Cache Miss）和总线争用，会引入抖动（Jitter）。PsPIN通过使用TCDM（暂存器）代替Cache来缓解这一问题。  
* **吞吐量**：单个RISC-V核心的处理能力有限（通常在1-2 IPC）。为了匹配100Gbps+的线速，必须采用大规模多核并行（如32核以上）和硬件辅助的任务调度16。

### **6.2 未来方向**

1. **Chiplet（芯粒）互联**：未来的P4交换芯片可能不再单片集成RISC-V，而是通过UCIe等标准接口连接高性能的RISC-V Chiplet，实现计算能力的灵活扩展。  
2. **统一编程模型**：目前P4与C/C++（RISC-V）是分离的。未来可能会出现统一的语言（如P4的扩展），允许开发者在同一源文件中定义流水线逻辑和计算核心逻辑，由编译器自动切分。

## **7\. 结论**

通过对通用P4架构和基于RISC-V的实现方式的详尽调研，本报告得出以下结论：PISA/PSA架构奠定了可编程数据平面的基础，但在处理复杂状态和计算密集型任务时存在瓶颈。将RISC-V核心集成到P4交换机中，特别是采用PsPIN这种共享内存、紧耦合集群的架构，有效地填补了“固定流水线”与“通用CPU”之间的空白。

从架构图文献来看，无论是学术界的PsPIN/FPsPIN，还是工业界的Pensando/BlueField，其核心设计理念均殊途同归：即利用P4流水线进行高效的流量分类和预处理，利用RISC-V集群进行灵活性极高的载荷处理。此外，OmniXtend的案例展示了P4交换机在构建大规模RISC-V计算集群互联中的独特价值。随着RISC-V指令集在向量计算和位操作上的不断扩展，以及P4编译器生态的成熟，这种异构数据平面架构将成为未来智能网络基础设施的主流形态。

---

**主要数据对比表：主流P4+RISC-V架构特征**

| 架构名称 | 宿主平台 | RISC-V核心集成方式 | 内存架构 | 典型应用场景 |
| :---- | :---- | :---- | :---- | :---- |
| **PsPIN** 15 | ASIC设计 (22nm FDSOI) | 紧耦合集群 (32核, 4集群) | L1 TCDM \+ L2 Shared Buffer | 流量聚合(MPI AllReduce), 在线压缩 |
| **FPsPIN** 19 | FPGA (NetFPGA-SUME) | 软核 (40 MHz) | FPGA BRAM \+ Host DRAM (DMA) | 学术原型验证, 复杂协议卸载 |
| **BlueField-3** 28 | SmartNIC SoC | DPA加速器 (16核 RISC-V) | 片上L1/L2 \+ HBM/DDR | 存储卸载(NVMe), 安全加密(IPSec) |
| **Pensando DSC** 26 | P4-Native SoC | MPU (流水线级处理器) | 流水线局部存储 \+ 全局表项 | 分布式防火墙, 遥测, 负载均衡 |
| **OmniXtend** 22 | P4 Switch (Tofino) | 外部互联 (Switch作为Hub) | 交换机SRAM (作为目录缓存) | RISC-V多节点缓存一致性互联 |

#### **Works cited**

1. Runtime Programmable Switches \- USENIX, accessed February 18, 2026, [https://www.usenix.org/system/files/nsdi22-paper-xing.pdf](https://www.usenix.org/system/files/nsdi22-paper-xing.pdf)  
2. P416 Portable Switch Architecture (PSA), accessed February 18, 2026, [https://p4lang.github.io/p4-spec/docs/PSA.pdf](https://p4lang.github.io/p4-spec/docs/PSA.pdf)  
3. Difference between P4 architectures \- PISA vs PSA, accessed February 18, 2026, [https://forum.p4.org/t/difference-between-p4-architectures-pisa-vs-psa/1240](https://forum.p4.org/t/difference-between-p4-architectures-pisa-vs-psa/1240)  
4. An Overview of P4 Programmable Switches and Applications \- University of South Carolina, accessed February 18, 2026, [https://research.cec.sc.edu/files/cyberinfra/files/BYU%20presentation.pdf](https://research.cec.sc.edu/files/cyberinfra/files/BYU%20presentation.pdf)  
5. DOCA Target Architecture \- NVIDIA Docs, accessed February 18, 2026, [https://docs.nvidia.com/doca/sdk/doca-target-architecture/index.html](https://docs.nvidia.com/doca/sdk/doca-target-architecture/index.html)  
6. P4TC hits a brick wall \- LWN.net, accessed February 18, 2026, [https://lwn.net/Articles/977310/](https://lwn.net/Articles/977310/)  
7. ControlPULP: A RISC-V On-Chip Parallel Power Controller for Many-Core HPC Processors with FPGA-Based Hardware-In-The-Loop Power and Thermal Emulation \- arXiv, accessed February 18, 2026, [https://arxiv.org/html/2306.09501v3](https://arxiv.org/html/2306.09501v3)  
8. Proceedings of the VLDB Endowment \- Horizon IRD, accessed February 18, 2026, [https://horizon.documentation.ird.fr/exl-doc/pleins\_textes/2024-03/010086183.pdf](https://horizon.documentation.ird.fr/exl-doc/pleins_textes/2024-03/010086183.pdf)  
9. Embedded Interview Questions Basics | PDF | Pointer (Computer Programming) \- Scribd, accessed February 18, 2026, [https://www.scribd.com/document/885741816/Embedded-interview-questions-basics](https://www.scribd.com/document/885741816/Embedded-interview-questions-basics)  
10. RISC-V Instruction Set Architecture Extensions: A Survey \- IEEE Xplore, accessed February 18, 2026, [https://ieeexplore.ieee.org/iel7/6287639/10005208/10049118.pdf](https://ieeexplore.ieee.org/iel7/6287639/10005208/10049118.pdf)  
11. Why do people not implement the RISC-V vector extension within the core? \- Reddit, accessed February 18, 2026, [https://www.reddit.com/r/RISCV/comments/18z6qe2/why\_do\_people\_not\_implement\_the\_riscv\_vector/](https://www.reddit.com/r/RISCV/comments/18z6qe2/why_do_people_not_implement_the_riscv_vector/)  
12. A RISC-V in-network accelerator for flexible high-performance low-power packet processing | Request PDF \- ResearchGate, accessed February 18, 2026, [https://www.researchgate.net/publication/353696477\_A\_RISC-V\_in-network\_accelerator\_for\_flexible\_high-performance\_low-power\_packet\_processing](https://www.researchgate.net/publication/353696477_A_RISC-V_in-network_accelerator_for_flexible_high-performance_low-power_packet_processing)  
13. HOL4P4.EXE: A Formally Verified P4 Software Switch \- SYSTEMF @ EPFL, accessed February 18, 2026, [https://systemf.epfl.ch/etc/vstte2025/preprints/HOL4P4.EXE:%20A%20Formally%20Verified%20P4%20Software%20Switch.pdf](https://systemf.epfl.ch/etc/vstte2025/preprints/HOL4P4.EXE:%20A%20Formally%20Verified%20P4%20Software%20Switch.pdf)  
14. PsPIN switch high-level architecture. | Download Scientific Diagram \- ResearchGate, accessed February 18, 2026, [https://www.researchgate.net/figure/PsPIN-switch-high-level-architecture\_fig2\_356188489](https://www.researchgate.net/figure/PsPIN-switch-high-level-architecture_fig2_356188489)  
15. (PDF) PsPIN: A high-performance low-power architecture for flexible in-network compute, accessed February 18, 2026, [https://www.researchgate.net/publication/344530072\_PsPIN\_A\_high-performance\_low-power\_architecture\_for\_flexible\_in-network\_compute](https://www.researchgate.net/publication/344530072_PsPIN_A_high-performance_low-power_architecture_for_flexible_in-network_compute)  
16. PsPIN: A high-performance low-power architecture for flexible in-network compute, accessed February 18, 2026, [https://www.semanticscholar.org/paper/PsPIN%3A-A-high-performance-low-power-architecture-Girolamo-Kurth/04151551542ef772c37d70658e33e29a0a02f898](https://www.semanticscholar.org/paper/PsPIN%3A-A-high-performance-low-power-architecture-Girolamo-Kurth/04151551542ef772c37d70658e33e29a0a02f898)  
17. Full-System Evaluation of the sPIN In-Network-Compute Architecture \- ETH Library, accessed February 18, 2026, [https://www.research-collection.ethz.ch/bitstreams/acb8abb0-449f-44b6-a169-6f91deaedebe/download](https://www.research-collection.ethz.ch/bitstreams/acb8abb0-449f-44b6-a169-6f91deaedebe/download)  
18. MiniFloat-NN and ExSdotp: An ISA Extension and a Modular Open Hardware Unit for Low-Precision Training on RISC-V Cores, accessed February 18, 2026, [https://www.research-collection.ethz.ch/bitstreams/baadddaf-efc2-4c13-9448-363a2d5bf8d8/download](https://www.research-collection.ethz.ch/bitstreams/baadddaf-efc2-4c13-9448-363a2d5bf8d8/download)  
19. (PDF) FPsPIN: An FPGA-based Open-Hardware Research Platform ..., accessed February 18, 2026, [https://www.researchgate.net/publication/380906827\_FPsPIN\_An\_FPGA-based\_Open-Hardware\_Research\_Platform\_for\_Processing\_in\_the\_Network](https://www.researchgate.net/publication/380906827_FPsPIN_An_FPGA-based_Open-Hardware_Research_Platform_for_Processing_in_the_Network)  
20. ControlPULP: A RISC-V On- Chip Parallel Power Controller for Many-Core HPC Processors with FPGA-Based Hardware \- ETH Library, accessed February 18, 2026, [https://www.research-collection.ethz.ch/bitstreams/0f27efad-5d43-4a3a-8432-db9d56efbc5f/download](https://www.research-collection.ethz.ch/bitstreams/0f27efad-5d43-4a3a-8432-db9d56efbc5f/download)  
21. ControlPULP hardware architecture. On the left, the manager domain with... \- ResearchGate, accessed February 18, 2026, [https://www.researchgate.net/figure/ControlPULP-hardware-architecture-On-the-left-the-manager-domain-with-the-manager-core\_fig3\_378494081](https://www.researchgate.net/figure/ControlPULP-hardware-architecture-On-the-left-the-manager-domain-with-the-manager-core_fig3_378494081)  
22. OmniXtend cache coherence protocol \- GitHub, accessed February 18, 2026, [https://github.com/chipsalliance/omnixtend](https://github.com/chipsalliance/omnixtend)  
23. Open Memory-Centric Architectures Enabled by RISC-V and OmniXtend \- Industry Articles, accessed February 18, 2026, [https://www.allaboutcircuits.com/industry-articles/open-memory-centric-architectures-enabled-by-risc-v-and-omnixtend/](https://www.allaboutcircuits.com/industry-articles/open-memory-centric-architectures-enabled-by-risc-v-and-omnixtend/)  
24. Chips alliance omni xtend overview | PPTX \- Slideshare, accessed February 18, 2026, [https://www.slideshare.net/slideshow/chips-alliance-omni-xtend-overview/246459005](https://www.slideshare.net/slideshow/chips-alliance-omni-xtend-overview/246459005)  
25. Tech Brief: RISC-V: Configurability & Openness for a Data-Centric Computing Architecture \- Western Digital, accessed February 18, 2026, [https://documents.westerndigital.com/content/dam/doc-library/en\_us/assets/public/western-digital/collateral/tech-brief/tech-brief-western-digital-risc-v.pdf](https://documents.westerndigital.com/content/dam/doc-library/en_us/assets/public/western-digital/collateral/tech-brief/tech-brief-western-digital-risc-v.pdf)  
26. AMD Delivers 400GbE Speeds With Industry's First "UEC-Ready" Pensando Pollara 400 AI NIC \- Wccftech, accessed February 18, 2026, [https://wccftech.com/amd-400gbe-speeds-industrys-first-uec-ready-pensando-pollara-400-ai-nic/](https://wccftech.com/amd-400gbe-speeds-industrys-first-uec-ready-pensando-pollara-400-ai-nic/)  
27. US20220417142A1 \- Methods and systems for removing expired flow table entries using an extended packet processing pipeline \- Google Patents, accessed February 18, 2026, [https://patents.google.com/patent/US20220417142A1/en](https://patents.google.com/patent/US20220417142A1/en)  
28. Nvidia BlueField-3 SmartNIC Overview, accessed February 18, 2026, [https://www.emergentmind.com/topics/bluefield-3-smartnic](https://www.emergentmind.com/topics/bluefield-3-smartnic)  
29. NVIDIA BlueField-3 SNAP for NVMe and Virtio-blk v4.5.0, accessed February 18, 2026, [https://docs.nvidia.com/networking/display/nvidia-bluefield-3-snap-for-nvme-and-virtio-blk-v4-5-0.0.pdf](https://docs.nvidia.com/networking/display/nvidia-bluefield-3-snap-for-nvme-and-virtio-blk-v4-5-0.0.pdf)  
30. November 13-15 2018, Vancouver, BC \- Linux Plumbers Conference, accessed February 18, 2026, [https://lpc.events/event/2/timetable/?view=standard](https://lpc.events/event/2/timetable/?view=standard)  
31. Fully Programming the Data Plane: A Hardware ... \- PolyPublie, accessed February 18, 2026, [https://publications.polymtl.ca/5215/1/2020\_JefersonSantiagoDaSilva.pdf](https://publications.polymtl.ca/5215/1/2020_JefersonSantiagoDaSilva.pdf)  
32. Proceedings of the Seminar Innovative Internet Technologies and Mobile Communications (IITM), Summer Semester 2023 \- Chair of Network Architectures and Services \- TUM, accessed February 18, 2026, [https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2023-11-1.pdf](https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2023-11-1.pdf)  
33. Towards AI-based Network Programmability as an enabler for Zero-touch management and orchestration in B5G infrastructures \- Fundación Séneca, accessed February 18, 2026, [https://fseneca.es/cms/sites/default/files/Gallego-Madrid-Jorge\_TD\_2024.pdf](https://fseneca.es/cms/sites/default/files/Gallego-Madrid-Jorge_TD_2024.pdf)