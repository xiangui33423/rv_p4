# 环境安装与配置脚本

初始化和配置 RV-P4 ASIC 设计环境。

## 🚀 快速安装

### 完全新手？一键安装所有工具

```bash
cd /home/serve-ide/rv_p4/scripts/setup
./install_asic_tools.sh
```

此脚本会自动：
- 下载并安装 Yosys、OpenROAD、OpenSTA
- 编译 Magic、Netgen、KLayout
- 配置 Sky130 PDK
- 设置环境变量

### 只需要 PDK？

```bash
./setup_pdk.sh
```

### 只需要编译香山核？

```bash
./build_xiangshan.sh
```

---

## 📋 各脚本详解

### 1. install_asic_tools.sh

**功能**：完整的开源 ASIC 工具链安装

**安装工具**：
- ✅ Yosys - 逻辑综合（ISC License）
- ✅ OpenROAD - Place & Route（Apache 2.0）
- ✅ OpenSTA - 静态时序分析（Apache 2.0）
- ✅ Magic - DRC/LVS/GDS（GPL）
- ✅ Netgen - LVS 验证（GPL）
- ✅ KLayout - GDS 查看器（GPL）

**安装时间**：20-40 分钟（取决于网络和硬件）

**系统要求**：
- Ubuntu/Debian Linux
- 4GB RAM（推荐 8GB+）
- 10GB 硬盘空间（推荐 20GB+）

**用法**：
```bash
./install_asic_tools.sh

# 或指定安装路径
PREFIX=/opt/asic-tools ./install_asic_tools.sh
```

**主要步骤**：
```
1. 检查系统依赖
2. 克隆或下载源代码
3. 编译各工具
4. 配置环境变量
5. 验证安装
```

**输出**：
```bash
# 工具位置
/usr/local/bin/yosys
/usr/local/bin/openroad
/usr/local/bin/opensta
/usr/bin/magic
/usr/bin/netgen
/usr/bin/klayout
```

**验证安装**：
```bash
yosys -v
openroad -h
opensta -h
magic -v
netgen -v
klayout -v
```

**故障排除**：
```bash
# 如果安装失败，检查依赖
sudo apt-get install build-essential tcl tcl-dev
sudo apt-get install libboost-all-dev libdb-dev
sudo apt-get install bison flex

# 重新运行安装
./install_asic_tools.sh
```

### 2. setup_pdk.sh

**功能**：Sky130 工艺库（PDK）配置

**安装内容**：
- ✅ Sky130 工艺库文件
- ✅ 标准单元库（lib, lef, spice）
- ✅ 配置文件和宏库

**库文件**：
```
sky130/
├── libs.ref/
│   ├── sky130_fd_sc_hd/          # High-Density 库
│   ├── sky130_fd_sc_hs/          # High-Speed 库
│   ├── sky130_fd_sc_lp/          # Low-Power 库
│   └── sky130_sram_*_*_hd/       # SRAM 宏库
├── libs.tech/
│   ├── magic/
│   ├── klayout/
│   └── libresilicon/
└── tech.lef                       # 工艺 LEF
```

**用法**：
```bash
./setup_pdk.sh

# 或指定下载路径
PDK_ROOT=/path/to/pdk ./setup_pdk.sh
```

**下载来源**：
- Google + SkyWater 开源项目
- Apache 2.0 许可证
- 完全免费

**下载大小**：约 2-3 GB

**验证安装**：
```bash
ls -la $PDK_ROOT/sky130/libs.ref/
ls -la $PDK_ROOT/sky130/libs.tech/
```

### 3. build_xiangshan.sh

**功能**：编译香山 RISC-V 核（作为 RV-P4 控制面）

**用途**：
- 如需从源代码编译香山
- 生成 RTL 代码供集成
- 可选脚本（已有子模块可不运行）

**系统要求**：
- Scala 编译器
- Java JDK
- Chisel HDL 框架

**用法**：
```bash
./build_xiangshan.sh

# 或启用特定选项
XIANGSHAN_VERSION=main ./build_xiangshan.sh
```

**编译时间**：30-60 分钟

**输出**：
```
XiangShan/
├── build/
│   └── xiangshan_nanhu.v     # Verilog 文件
└── generated_src/
    └── xiangshan_*.sv         # SystemVerilog 文件
```

**跳过此步**：
```bash
# RV-P4 已包含 XiangShan 子模块，通常不需要运行此脚本
git submodule update --init XiangShan/
```

---

## ⚙️ 环境配置

### 自动配置（推荐）

```bash
# 运行安装脚本后，自动添加到 ~/.bashrc
source ~/.bashrc

# 验证工具可用
which yosys
which openroad
which opensta
```

### 手动配置

```bash
# 编辑 ~/.bashrc
export PATH="/usr/local/bin:$PATH"
export PDK_ROOT="/home/$(whoami)/pdk"
export YOSYS_SHARE="/usr/local/share/yosys"
export OPENROAD_EXE="/usr/local/bin/openroad"

# 应用配置
source ~/.bashrc
```

### Docker 方式（可选）

如果不想污染本机环境，可以使用 Docker：

```bash
# 构建 Docker 镜像
docker build -t rv-p4-asic:latest -f Dockerfile .

# 运行容器
docker run -it -v $(pwd):/work rv-p4-asic:latest bash
```

---

## 🔍 安装验证

### 完整验证脚本

```bash
#!/bin/bash
# 验证所有工具

echo "=== ASIC 工具链验证 ==="

tools=("yosys" "openroad" "opensta" "magic" "netgen" "klayout")

for tool in "${tools[@]}"; do
    if command -v $tool &> /dev/null; then
        echo "✓ $tool: $(which $tool)"
        $tool -v 2>&1 | head -1
    else
        echo "✗ $tool: 未找到"
    fi
done

# 验证 PDK
if [ -d "$PDK_ROOT/sky130" ]; then
    echo "✓ PDK: $PDK_ROOT/sky130"
else
    echo "✗ PDK: 未配置"
fi

echo ""
echo "=== 验证完成 ==="
```

### 个别工具验证

```bash
# Yosys
yosys -v
yosys -p "help"

# OpenROAD
openroad -h

# OpenSTA
opensta -help

# Magic
magic -v

# Netgen
netgen -v

# KLayout
klayout --version
```

---

## 🐛 常见问题

### Q: 找不到 Yosys

**A:**
```bash
# 检查是否安装
which yosys

# 如未安装，运行
./install_asic_tools.sh

# 手动添加到 PATH
export PATH="/usr/local/bin:$PATH"
```

### Q: PDK 很大，下载太慢

**A:**
```bash
# 只下载 HD 库（最常用）
PDK_LIBS="sky130_fd_sc_hd" ./setup_pdk.sh

# 或预先下载后指定本地路径
PDK_ROOT=/path/to/local/pdk ./setup_pdk.sh
```

### Q: 编译失败，缺少依赖

**A:**
```bash
# 安装基本依赖
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    git \
    cmake \
    tcl tcl-dev \
    libboost-all-dev \
    libdb-dev \
    bison flex

# 重新运行安装
./install_asic_tools.sh
```

### Q: 权限不足

**A:**
```bash
# 使用 sudo 运行安装（不推荐）
sudo ./install_asic_tools.sh

# 或添加当前用户到 docker 组
sudo usermod -aG docker $(whoami)
newgrp docker
```

---

## 📦 安装后的目录结构

```
/usr/local/
├── bin/
│   ├── yosys
│   ├── openroad
│   ├── opensta
│   └── ...
├── share/
│   ├── yosys/
│   ├── openroad/
│   └── ...
└── lib/
    └── ...

$PDK_ROOT/
└── sky130/
    ├── libs.ref/
    ├── libs.tech/
    └── ...

$HOME/
├── .bashrc (添加 PATH/PDK_ROOT)
└── ...
```

---

## 🔗 相关资源

### 官方文档
- [Yosys 文档](http://www.clifford.at/yosys/)
- [OpenROAD 文档](https://openroad.readthedocs.io/)
- [Sky130 PDK](https://github.com/google/skywater-pdk)

### 项目文档
- `../../docs/03-asic-flow/GDS_II_QUICK_START.md` - 快速开始
- `../../docs/03-asic-flow/GDS_II_AUTOMATION_GUIDE.md` - 完整指南

---

## ⏭️ 后续步骤

1. **验证安装** → 运行上面的验证脚本
2. **运行 ASIC 流程** → 进入 `../asic-flow/` 目录
3. **查看文档** → 查阅 `../../docs/`

---

**最后更新**：2026-02-19

**⚠️ 注意**：
- 首次完整安装需要 30-40 分钟
- 需要稳定的网络连接
- 需要足够的磁盘空间（20GB+）
- 建议在稳定的电源下进行
