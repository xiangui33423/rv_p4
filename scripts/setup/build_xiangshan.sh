#!/usr/bin/env bash
# build_xiangshan.sh
# RV-P4 × 香山 CPU 集成构建脚本
#
# 功能：
#   1. 安装依赖（JDK 17、Mill、RISC-V 工具链）
#   2. 克隆香山仓库（含子模块）
#   3. 生成 XSTop.v（Chisel → Verilog）
#   4. 提取顶层端口，替换 ctrl_plane.sv 中的黑盒
#   5. 重新编译 Verilator 顶层仿真
#
# 用法：
#   bash scripts/build_xiangshan.sh [--skip-clone] [--skip-build] [--jobs N]
#
# 环境变量：
#   XS_DIR      香山克隆目录（默认 ./xiangshan）
#   XS_BRANCH   香山分支（默认 master）
#   JOBS        并行编译线程数（默认 $(nproc)）

set -euo pipefail

# ─────────────────────────────────────────────
# 参数解析
# ─────────────────────────────────────────────
SKIP_CLONE=0
SKIP_BUILD=0
JOBS=${JOBS:-$(nproc)}
XS_DIR=${XS_DIR:-"$(pwd)/xiangshan"}
XS_BRANCH=${XS_BRANCH:-"master"}

for arg in "$@"; do
    case $arg in
        --skip-clone) SKIP_CLONE=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --jobs)       shift; JOBS=$1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

log()  { echo "[$(date '+%H:%M:%S')] $*"; }
die()  { echo "ERROR: $*" >&2; exit 1; }
step() { echo; echo "══════════════════════════════════════"; echo "  $*"; echo "══════════════════════════════════════"; }

# ─────────────────────────────────────────────
# Step 1: 安装依赖
# ─────────────────────────────────────────────
step "Step 1: 检查并安装依赖"

install_jdk() {
    if java -version 2>&1 | grep -q "17\|21"; then
        log "JDK 已安装: $(java -version 2>&1 | head -1)"
        return
    fi
    log "安装 OpenJDK 17..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y openjdk-17-jdk
    elif command -v yum &>/dev/null; then
        sudo yum install -y java-17-openjdk-devel
    else
        die "不支持的包管理器，请手动安装 JDK 17"
    fi
}

install_mill() {
    if command -v mill &>/dev/null; then
        log "Mill 已安装: $(mill --version 2>&1 | head -1)"
        return
    fi
    log "安装 Mill 构建工具..."
    curl -L https://github.com/com-lihaoyi/mill/releases/download/0.11.7/mill \
         -o /usr/local/bin/mill
    chmod +x /usr/local/bin/mill
}

install_riscv_toolchain() {
    if command -v riscv64-unknown-linux-gnu-gcc &>/dev/null; then
        log "RISC-V GCC 已安装"
        return
    fi
    log "安装 RISC-V GCC 工具链..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get install -y gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
    else
        # 从 SiFive 下载预编译工具链
        TOOLCHAIN_URL="https://github.com/sifive/freedom-tools/releases/download/v2020.12.0/riscv64-unknown-elf-gcc-10.1.0-2020.12.8-x86_64-linux-ubuntu14.tar.gz"
        log "下载 RISC-V 工具链（约 200MB）..."
        curl -L "$TOOLCHAIN_URL" | tar -xz -C /opt/
        echo 'export PATH=/opt/riscv64-unknown-elf-gcc-10.1.0-2020.12.8-x86_64-linux-ubuntu14/bin:$PATH' \
             >> ~/.bashrc
    fi
}

install_verilator() {
    if command -v verilator &>/dev/null; then
        log "Verilator 已安装: $(verilator --version | head -1)"
        return
    fi
    log "安装 Verilator..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get install -y verilator
    else
        die "请手动安装 Verilator 5.x"
    fi
}

install_jdk
install_mill
install_riscv_toolchain
install_verilator

# ─────────────────────────────────────────────
# Step 2: 克隆香山仓库
# ─────────────────────────────────────────────
step "Step 2: 克隆香山仓库"

if [[ $SKIP_CLONE -eq 0 ]]; then
    if [[ -d "$XS_DIR/.git" ]]; then
        log "香山仓库已存在，执行 git pull..."
        git -C "$XS_DIR" pull --rebase
    else
        log "克隆香山仓库（含子模块，约 3GB）..."
        git clone --recursive \
            --branch "$XS_BRANCH" \
            --depth 1 \
            https://github.com/OpenXiangShan/XiangShan.git \
            "$XS_DIR"
    fi
    log "更新子模块..."
    git -C "$XS_DIR" submodule update --init --recursive --depth 1
else
    log "跳过克隆（--skip-clone）"
    [[ -d "$XS_DIR" ]] || die "XS_DIR=$XS_DIR 不存在"
fi

# ─────────────────────────────────────────────
# Step 3: 生成 XSTop.v
# ─────────────────────────────────────────────
step "Step 3: 生成 XSTop.v（Chisel → Verilog）"

XS_VERILOG_DIR="$XS_DIR/build"
XS_TOP_V="$XS_VERILOG_DIR/XSTop.v"

if [[ $SKIP_BUILD -eq 0 ]]; then
    log "编译香山（约 30-60 分钟，使用 $JOBS 线程）..."
    cd "$XS_DIR"

    # 香山使用 make verilog 生成 Verilog
    # CONFIG=MinimalConfig 用于快速验证（完整配置用 DefaultConfig）
    make verilog \
        CONFIG=MinimalConfig \
        MFC=1 \
        JOBS="$JOBS" \
        2>&1 | tee "$PROJECT_DIR/scripts/xs_build.log"

    [[ -f "$XS_TOP_V" ]] || die "XSTop.v 未生成，查看 scripts/xs_build.log"
    log "XSTop.v 生成成功: $(wc -l < "$XS_TOP_V") 行"
    cd "$PROJECT_DIR"
else
    log "跳过构建（--skip-build）"
    [[ -f "$XS_TOP_V" ]] || die "XSTop.v 不存在: $XS_TOP_V"
fi

# ─────────────────────────────────────────────
# Step 4: 提取香山顶层端口，更新黑盒
# ─────────────────────────────────────────────
step "Step 4: 更新 ctrl_plane.sv 黑盒接口"

python3 "$SCRIPT_DIR/gen_xs_blackbox.py" \
    --xs-top "$XS_TOP_V" \
    --ctrl-plane "$PROJECT_DIR/rtl/ctrl/ctrl_plane.sv" \
    --output "$PROJECT_DIR/rtl/ctrl/ctrl_plane_xs.sv"

log "生成 ctrl_plane_xs.sv（含真实香山接口）"

# ─────────────────────────────────────────────
# Step 5: 重新编译顶层仿真
# ─────────────────────────────────────────────
step "Step 5: 重新编译 Verilator 顶层仿真"

cd "$PROJECT_DIR"

# 收集所有 RTL 文件（用 ctrl_plane_xs.sv 替换 ctrl_plane.sv）
RTL_FILES=$(find rtl -name "*.sv" \
    ! -name "ctrl_plane.sv" \
    | sort | tr '\n' ' ')
RTL_FILES="$RTL_FILES rtl/ctrl/ctrl_plane_xs.sv"

# 香山生成的 Verilog（可能很大，单独列出）
XS_FILES=$(find "$XS_VERILOG_DIR" -name "*.v" -o -name "*.sv" 2>/dev/null \
    | grep -v "test\|sim" | sort | tr '\n' ' ')

log "编译 Verilator 顶层仿真..."
rm -rf obj_dir/tb_top_xs
mkdir -p obj_dir/tb_top_xs

verilator --binary -sv --timing \
    -Wno-TIMESCALEMOD -Wno-MULTIDRIVEN -Wno-WAITCONST \
    -Wno-WIDTHEXPAND -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL \
    +incdir+rtl/include \
    $RTL_FILES \
    $XS_FILES \
    tb/top/tb_top.sv \
    --top-module tb_top \
    -Mdir obj_dir/tb_top_xs \
    -j "$JOBS" \
    2>&1 | tee scripts/verilator_xs.log

log "编译完成"

# ─────────────────────────────────────────────
# Step 6: 运行集成仿真
# ─────────────────────────────────────────────
step "Step 6: 运行集成仿真"

obj_dir/tb_top_xs/Vtb_top 2>&1 | tee scripts/sim_xs.log
grep -E "PASS|FAIL|===" scripts/sim_xs.log

log "集成构建完成！"
log "日志文件："
log "  scripts/xs_build.log    — 香山编译日志"
log "  scripts/verilator_xs.log — Verilator 编译日志"
log "  scripts/sim_xs.log       — 仿真日志"
