#!/usr/bin/env python3
"""
rvp4cc.py — RV-P4 C-to-HW 编译器
将带 __attribute__ 标注的 C 数据面代码编译为硬件配置文件

用法:
    python3 rvp4cc.py <input.c> -o <output.hwcfg> [--report]

输出:
    parser_tcam.bin          Parser TCAM 初始化镜像
    mau_tcam_init_s{N}.bin   每级 TCAM 初始化镜像
    mau_asram_init_s{N}.bin  每级 Action SRAM 初始化镜像
    phv_map.json             PHV 字段映射表
    table_info.json          表元信息
    action_info.json         动作元信息
    dataplane.hwcfg          上述文件的 tar.gz 打包
    dataplane.report         编译报告
"""

import re
import sys
import os
import json
import struct
import tarfile
import argparse
import io
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ─────────────────────────────────────────────
# 硬件常量（与 rv_p4_pkg.sv 一致）
# ─────────────────────────────────────────────
NUM_MAU_STAGES      = 24
PARSER_TCAM_DEPTH   = 256
PARSER_TCAM_WIDTH   = 640   # bits = 80 bytes
MAU_TCAM_DEPTH      = 2048
MAU_TCAM_KEY_W      = 512   # bits = 64 bytes
MAU_ASRAM_DEPTH     = 65536
MAU_ASRAM_WIDTH     = 128   # bits = 16 bytes
PHV_BYTES           = 512

# ALU 操作码（与 mau_alu.sv 一致）
OP_NOP      = 0x0
OP_SET      = 0x1
OP_COPY     = 0x2
OP_ADD      = 0x3
OP_SUB      = 0x4
OP_AND      = 0x5
OP_OR       = 0x6
OP_XOR      = 0x7
OP_SET_META = 0x8
OP_DROP     = 0x9
OP_SET_PORT = 0xA
OP_SET_PRIO = 0xB
OP_HASH_SET = 0xC
OP_COND_SET = 0xD

# PHV 字段偏移（与 table_map.h 一致）
PHV_FIELDS = {
    "eth_dst":      (0,  6),
    "eth_src":      (6,  6),
    "eth_type":     (12, 2),
    "vlan_tci":     (14, 2),
    "ipv4_ihl":     (18, 1),
    "ipv4_dscp":    (19, 1),
    "ipv4_tot_len": (20, 2),
    "ipv4_ttl":     (26, 1),
    "ipv4_proto":   (27, 1),
    "ipv4_cksum":   (28, 2),
    "ipv4_src":     (30, 4),
    "ipv4_dst":     (34, 4),
    "tcp_sport":    (38, 2),
    "tcp_dport":    (40, 2),
    "udp_sport":    (38, 2),
    "udp_dport":    (40, 2),
}

# ─────────────────────────────────────────────
# 数据结构
# ─────────────────────────────────────────────

@dataclass
class Attribute:
    name: str
    args: List[str] = field(default_factory=list)

@dataclass
class ActionPrimitive:
    op: int
    dst_off: int = 0
    src_off: int = 0
    imm_val: int = 0
    fwidth:  int = 4

@dataclass
class ActionDef:
    name:       str
    action_id:  int
    primitives: List[ActionPrimitive] = field(default_factory=list)
    params:     List[str] = field(default_factory=list)

@dataclass
class TableDef:
    name:       str
    stage:      int
    table_id:   int
    match_type: str   # "lpm" | "exact" | "ternary"
    size:       int
    key_fields: List[str] = field(default_factory=list)
    actions:    List[str] = field(default_factory=list)

@dataclass
class ParserState:
    name:           str
    state_id:       int
    key_offset:     int   # byte offset in cell to match
    key_len:        int
    key_val:        int
    key_mask:       int
    next_state:     int
    extract_offset: int
    extract_len:    int
    phv_dst_offset: int
    hdr_advance:    int

# ─────────────────────────────────────────────
# 词法/语法分析
# ─────────────────────────────────────────────

def strip_comments(src: str) -> str:
    """去除 C 注释"""
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.DOTALL)
    src = re.sub(r'//[^\n]*', '', src)
    return src

def parse_attributes(text: str) -> List[Attribute]:
    """提取 __attribute__((name(args...))) 列表"""
    attrs = []
    for m in re.finditer(r'__attribute__\s*\(\s*\(\s*(\w+)(?:\s*\(([^)]*)\))?\s*\)\s*\)', text):
        name = m.group(1)
        args = [a.strip() for a in m.group(2).split(',')] if m.group(2) else []
        attrs.append(Attribute(name, args))
    return attrs

def parse_functions(src: str) -> List[dict]:
    """提取带 __attribute__ 的函数定义"""
    funcs = []
    # 匹配：一个或多个 __attribute__ 块（可跨行），后跟函数签名
    pattern = re.compile(
        r'((?:__attribute__\s*\(\s*\((?:[^()]|\([^()]*\))*\)\s*\)\s*\n?)+)'
        r'\s*(?:[\w\s\*]+?\s+)?'   # return type (optional)
        r'(\w+)\s*\('              # function name
        r'([^)]*)\)',              # params
        re.DOTALL
    )
    for m in pattern.finditer(src):
        attr_text = m.group(1)
        fname     = m.group(2)
        params    = m.group(3)
        attrs     = parse_attributes(attr_text)
        if attrs:
            funcs.append({'name': fname, 'attrs': attrs, 'params': params})
    return funcs

def get_attr(attrs: List[Attribute], name: str) -> Optional[Attribute]:
    for a in attrs:
        if a.name == name:
            return a
    return None

# ─────────────────────────────────────────────
# Action 编码
# ─────────────────────────────────────────────

def encode_action_params(primitives: List[ActionPrimitive]) -> bytes:
    """
    将动作原语列表编码为 14B action_params（112b）
    格式（与 mau_alu.sv 一致）：
      [111:102] dst_offset (10b)
      [79:70]   src_offset (10b)
      [47:16]   imm_value  (32b)
      [15:8]    field_width (8b)
    取第一个原语（简化：每动作一个原语）
    """
    if not primitives:
        return bytes(14)
    p = primitives[0]
    val = 0
    val |= (p.dst_off & 0x3FF) << 102
    val |= (p.src_off & 0x3FF) << 70
    val |= (p.imm_val & 0xFFFFFFFF) << 16
    val |= (p.fwidth  & 0xFF) << 8
    return val.to_bytes(14, 'little')

def make_action_id(op: int, field_group: int = 0, sub_op: int = 0) -> int:
    return ((op & 0xF) << 12) | ((field_group & 0xF) << 8) | (sub_op & 0xFF)

# ─────────────────────────────────────────────
# 内置动作库
# ─────────────────────────────────────────────

BUILTIN_ACTIONS: Dict[str, ActionDef] = {
    "forward": ActionDef(
        name="forward", action_id=0x1001,
        primitives=[ActionPrimitive(op=OP_SET_PORT, dst_off=0, imm_val=0, fwidth=1)],
        params=["port"]
    ),
    "drop": ActionDef(
        name="drop", action_id=0x1002,
        primitives=[ActionPrimitive(op=OP_DROP)],
    ),
    "permit": ActionDef(
        name="permit", action_id=0x2001,
        primitives=[ActionPrimitive(op=OP_NOP)],
    ),
    "deny": ActionDef(
        name="deny", action_id=0x2002,
        primitives=[ActionPrimitive(op=OP_DROP)],
    ),
    "l2_forward": ActionDef(
        name="l2_forward", action_id=0x3001,
        primitives=[ActionPrimitive(op=OP_SET_PORT, dst_off=0, imm_val=0, fwidth=1)],
        params=["port"]
    ),
    "flood": ActionDef(
        name="flood", action_id=0x3002,
        primitives=[ActionPrimitive(op=OP_NOP)],
    ),
    "set_ttl_dec": ActionDef(
        name="set_ttl_dec", action_id=0x4001,
        primitives=[ActionPrimitive(op=OP_ADD, dst_off=PHV_FIELDS["ipv4_ttl"][0],
                                    imm_val=0xFFFFFFFF, fwidth=1)],  # -1 via wrap
    ),
    "set_dscp": ActionDef(
        name="set_dscp", action_id=0x4002,
        primitives=[ActionPrimitive(op=OP_SET, dst_off=PHV_FIELDS["ipv4_dscp"][0],
                                    imm_val=0, fwidth=1)],
        params=["dscp"]
    ),
    "nop": ActionDef(
        name="nop", action_id=0x0000,
        primitives=[ActionPrimitive(op=OP_NOP)],
    ),
}

# ─────────────────────────────────────────────
# 编译器主类
# ─────────────────────────────────────────────

class RVP4Compiler:
    def __init__(self):
        self.tables:  Dict[str, TableDef]  = {}
        self.actions: Dict[str, ActionDef] = dict(BUILTIN_ACTIONS)
        self.parser_states: List[ParserState] = []
        self.warnings: List[str] = []
        self.errors:   List[str] = []
        self._next_table_id = 0
        self._next_action_id = 0x5000
        self._next_state_id  = 1

    def compile(self, src: str):
        src = strip_comments(src)
        funcs = parse_functions(src)
        for f in funcs:
            self._process_function(f)
        self._assign_stages()
        self._validate()

    def _process_function(self, f: dict):
        attrs = f['attrs']
        name  = f['name']

        if get_attr(attrs, 'rvp4_table'):
            self._register_table(name, attrs)
        elif get_attr(attrs, 'rvp4_action'):
            self._register_action(name, attrs)
        elif get_attr(attrs, 'rvp4_parser'):
            self._register_parser_state(name, attrs)

    def _register_table(self, name: str, attrs: List[Attribute]):
        stage_attr = get_attr(attrs, 'rvp4_stage')
        stage = int(stage_attr.args[0]) if stage_attr and stage_attr.args else -1

        size_attr = get_attr(attrs, 'rvp4_size')
        size = int(size_attr.args[0]) if size_attr and size_attr.args else 256

        match_type = "ternary"
        if get_attr(attrs, 'rvp4_lpm'):    match_type = "lpm"
        if get_attr(attrs, 'rvp4_exact'):  match_type = "exact"
        if get_attr(attrs, 'rvp4_ternary'): match_type = "ternary"

        tid = self._next_table_id
        self._next_table_id += 1

        self.tables[name] = TableDef(
            name=name, stage=stage, table_id=tid,
            match_type=match_type, size=size
        )

    def _register_action(self, name: str, attrs: List[Attribute]):
        if name in self.actions:
            return
        aid = self._next_action_id
        self._next_action_id += 1
        self.actions[name] = ActionDef(name=name, action_id=aid)

    def _register_parser_state(self, name: str, attrs: List[Attribute]):
        sid = self._next_state_id
        self._next_state_id += 1
        # 默认：以太网帧头解析（state 1 = parse_ethernet）
        self.parser_states.append(ParserState(
            name=name, state_id=sid,
            key_offset=0, key_len=2, key_val=0, key_mask=0,
            next_state=0,  # ACCEPT
            extract_offset=0, extract_len=14,
            phv_dst_offset=0, hdr_advance=14
        ))

    def _assign_stages(self):
        """为未指定 stage 的表自动分配"""
        used = {t.stage for t in self.tables.values() if t.stage >= 0}
        next_free = 0
        for t in self.tables.values():
            if t.stage < 0:
                while next_free in used:
                    next_free += 1
                t.stage = next_free
                used.add(next_free)
                next_free += 1

    def _validate(self):
        for t in self.tables.values():
            if t.stage >= NUM_MAU_STAGES:
                self.errors.append(
                    f"Table '{t.name}' assigned to stage {t.stage} "
                    f"(max {NUM_MAU_STAGES-1})")
            if t.size > MAU_TCAM_DEPTH:
                self.warnings.append(
                    f"Table '{t.name}' size {t.size} exceeds TCAM depth "
                    f"{MAU_TCAM_DEPTH}, truncating")
                t.size = MAU_TCAM_DEPTH

    # ── 二进制生成 ────────────────────────────

    def gen_parser_tcam_bin(self) -> bytes:
        """
        Parser TCAM：256 条目 × 80 字节（640b）
        格式（简化）：
          [79:16] key+mask+extract（64B）
          [15:8]  next_state (8b)
          [7:0]   hdr_advance (8b)
        """
        entry_bytes = PARSER_TCAM_WIDTH // 8  # 80
        buf = bytearray(PARSER_TCAM_DEPTH * entry_bytes)

        # 默认条目 0：state=0（START），匹配任意 → 解析以太网头
        # key=0, mask=0（全通配），next_state=1，extract 14B 到 PHV[0]
        def write_entry(idx, key_val, key_mask, next_st, extract_off,
                        extract_len, phv_dst, hdr_adv):
            off = idx * entry_bytes
            # 简化编码：key(8B) mask(8B) next_state(1B) extract_off(1B)
            #            extract_len(1B) phv_dst_hi(1B) phv_dst_lo(1B)
            #            hdr_adv(1B) valid(1B) padding
            struct.pack_into('<QQBBBBBBBxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx',
                             buf, off,
                             key_val & 0xFFFFFFFFFFFFFFFF,
                             key_mask & 0xFFFFFFFFFFFFFFFF,
                             next_st & 0xFF,
                             extract_off & 0xFF,
                             extract_len & 0xFF,
                             (phv_dst >> 8) & 0xFF,
                             phv_dst & 0xFF,
                             hdr_adv & 0xFF,
                             1)  # valid

        # 条目 0：START → parse_ethernet（匹配任意）
        write_entry(0, 0, 0, 1, 0, 14, 0, 14)

        # 条目 1：parse_ethernet → 根据 EtherType 决定下一状态
        # EtherType=0x0800 (IPv4) → parse_ipv4
        write_entry(1, 0x0800, 0xFFFF, 2, 14, 20, 14, 20)

        # 条目 2：parse_ipv4 → ACCEPT
        write_entry(2, 0, 0, 0, 34, 8, 34, 0)

        # 用户自定义 parser 状态
        for i, ps in enumerate(self.parser_states):
            if 3 + i < PARSER_TCAM_DEPTH:
                write_entry(3 + i,
                            ps.key_val, ps.key_mask,
                            ps.next_state,
                            ps.extract_offset, ps.extract_len,
                            ps.phv_dst_offset, ps.hdr_advance)

        return bytes(buf)

    def gen_mau_tcam_bin(self, stage: int) -> bytes:
        """
        MAU TCAM：2048 条目 × 128 字节
        格式：key(64B) mask(64B) action_id(2B) action_ptr(2B) valid(1B) pad(55B)
        """
        entry_bytes = 128
        buf = bytearray(MAU_TCAM_DEPTH * entry_bytes)
        # 所有条目默认 invalid（全 0）
        return bytes(buf)

    def gen_mau_asram_bin(self, stage: int) -> bytes:
        """
        Action SRAM：65536 条目 × 16 字节
        格式：action_id(2B) params(14B)
        """
        entry_bytes = MAU_ASRAM_WIDTH // 8  # 16
        buf = bytearray(MAU_ASRAM_DEPTH * entry_bytes)

        # 写入内置动作到固定槽位（按 action_id 低 16b 索引）
        for act in self.actions.values():
            slot = act.action_id & 0xFFFF
            if slot < MAU_ASRAM_DEPTH:
                off = slot * entry_bytes
                params = encode_action_params(act.primitives)
                struct.pack_into('<H14s', buf, off,
                                 act.action_id & 0xFFFF,
                                 params)

        return bytes(buf)

    def gen_phv_map(self) -> dict:
        return {name: {"offset": off, "width": w}
                for name, (off, w) in PHV_FIELDS.items()}

    def gen_table_info(self) -> dict:
        return {
            name: {
                "stage":      t.stage,
                "table_id":   t.table_id,
                "match_type": t.match_type,
                "size":       t.size,
                "key_fields": t.key_fields,
                "actions":    t.actions,
            }
            for name, t in self.tables.items()
        }

    def gen_action_info(self) -> dict:
        return {
            name: {
                "action_id": a.action_id,
                "params":    a.params,
                "primitives": [
                    {"op": p.op, "dst_off": p.dst_off,
                     "src_off": p.src_off, "imm_val": p.imm_val,
                     "fwidth": p.fwidth}
                    for p in a.primitives
                ],
            }
            for name, a in self.actions.items()
        }

    def gen_report(self, src_file: str) -> str:
        lines = [
            "RV-P4 C-to-HW Compiler Report",
            "=" * 40,
            f"Source: {src_file}",
            f"Tables: {len(self.tables)}",
            f"Actions: {len(self.actions)}",
            f"Parser states: {len(self.parser_states) + 3}",
            "",
            "Table Allocation:",
        ]
        for t in sorted(self.tables.values(), key=lambda x: x.stage):
            lines.append(f"  [{t.stage:2d}] {t.name:<30s} "
                         f"{t.match_type:<8s} size={t.size}")
        lines += ["", "Actions:"]
        for a in self.actions.values():
            lines.append(f"  0x{a.action_id:04X}  {a.name}")
        if self.warnings:
            lines += ["", "Warnings:"] + [f"  W: {w}" for w in self.warnings]
        if self.errors:
            lines += ["", "Errors:"] + [f"  E: {e}" for e in self.errors]
        lines += ["", "Resource Usage:"]
        lines.append(f"  MAU stages used: "
                     f"{len({t.stage for t in self.tables.values()})}/{NUM_MAU_STAGES}")
        lines.append(f"  Parser TCAM entries: "
                     f"{len(self.parser_states)+3}/{PARSER_TCAM_DEPTH}")
        return "\n".join(lines) + "\n"

    def package(self, src_file: str, out_path: str):
        """打包所有输出文件为 .hwcfg (tar.gz)"""
        files: Dict[str, bytes] = {}

        files["parser_tcam.bin"] = self.gen_parser_tcam_bin()

        for s in range(NUM_MAU_STAGES):
            files[f"mau_tcam_init_s{s}.bin"]  = self.gen_mau_tcam_bin(s)
            files[f"mau_asram_init_s{s}.bin"] = self.gen_mau_asram_bin(s)

        files["phv_map.json"]     = json.dumps(self.gen_phv_map(),
                                               indent=2).encode()
        files["table_info.json"]  = json.dumps(self.gen_table_info(),
                                               indent=2).encode()
        files["action_info.json"] = json.dumps(self.gen_action_info(),
                                               indent=2).encode()
        files["dataplane.report"] = self.gen_report(src_file).encode()

        with tarfile.open(out_path, "w:gz") as tar:
            for name, data in files.items():
                info = tarfile.TarInfo(name=name)
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))

        return files

# ─────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="RV-P4 C-to-HW Compiler")
    parser.add_argument("input", help="Input C source file")
    parser.add_argument("-o", "--output", default="dataplane.hwcfg",
                        help="Output .hwcfg file (default: dataplane.hwcfg)")
    parser.add_argument("--report", action="store_true",
                        help="Print compilation report to stdout")
    parser.add_argument("--dump-json", action="store_true",
                        help="Dump JSON metadata files alongside .hwcfg")
    args = parser.parse_args()

    with open(args.input) as f:
        src = f.read()

    cc = RVP4Compiler()
    cc.compile(src)

    if cc.errors:
        for e in cc.errors:
            print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    files = cc.package(args.input, args.output)

    if args.report:
        print(cc.gen_report(args.input))

    if args.dump_json:
        out_dir = os.path.dirname(args.output) or "."
        for name in ("phv_map.json", "table_info.json",
                     "action_info.json", "dataplane.report"):
            with open(os.path.join(out_dir, name), "wb") as f:
                f.write(files[name])

    print(f"Compiled {args.input} → {args.output} "
          f"({os.path.getsize(args.output)} bytes)")
    if cc.warnings:
        for w in cc.warnings:
            print(f"warning: {w}", file=sys.stderr)

if __name__ == "__main__":
    main()
