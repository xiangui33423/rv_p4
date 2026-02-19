// table_map.h
// 由 C-to-HW 编译器自动生成 — 勿手动修改
// 数据面 C 代码编译后的表/动作 ID 映射

#ifndef TABLE_MAP_H
#define TABLE_MAP_H

// ─────────────────────────────────────────────
// MAU 级分配（编译器决定）
// ─────────────────────────────────────────────
#define TABLE_IPV4_LPM_STAGE        0    // ingress stage 0
#define TABLE_ACL_INGRESS_STAGE     1    // ingress stage 1
#define TABLE_L2_FDB_STAGE          2    // ingress stage 2

// ─────────────────────────────────────────────
// 表 ID 基地址（TCAM 条目索引起始）
// ─────────────────────────────────────────────
#define TABLE_IPV4_LPM_BASE         0x0000   // 最多 65536 条路由
#define TABLE_ACL_INGRESS_BASE      0x0000   // 最多 4096 条 ACL
#define TABLE_L2_FDB_BASE           0x0000   // 最多 32768 条 FDB

// ─────────────────────────────────────────────
// Action ID
// ─────────────────────────────────────────────

// ipv4_lpm 表的 action
#define ACTION_FORWARD              0x1001   // forward(port, dmac)
#define ACTION_DROP                 0x1002   // drop()

// acl_ingress 表的 action
#define ACTION_PERMIT               0x2001   // permit()
#define ACTION_DENY                 0x2002   // deny()

// l2_fdb 表的 action
#define ACTION_L2_FORWARD           0x3001   // l2_forward(port)
#define ACTION_FLOOD                0x3002   // flood()

// ─────────────────────────────────────────────
// PHV 字段偏移（字节，与 phv_t struct 对齐）
// ─────────────────────────────────────────────
#define PHV_OFF_ETH_DST             0
#define PHV_OFF_ETH_SRC             6
#define PHV_OFF_ETH_TYPE            12
#define PHV_OFF_VLAN_TCI            14
#define PHV_OFF_IPV4_VER_IHL        18
#define PHV_OFF_IPV4_DSCP           19
#define PHV_OFF_IPV4_TOT_LEN        20
#define PHV_OFF_IPV4_TTL            26
#define PHV_OFF_IPV4_PROTO          27
#define PHV_OFF_IPV4_SRC            30
#define PHV_OFF_IPV4_DST            34
#define PHV_OFF_TCP_SPORT           38
#define PHV_OFF_TCP_DPORT           40
#define PHV_OFF_UDP_SPORT           38
#define PHV_OFF_UDP_DPORT           40

// 元数据区偏移
#define PHV_OFF_IG_PORT             256
#define PHV_OFF_EG_PORT             257
#define PHV_OFF_DROP                258
#define PHV_OFF_PRIORITY            259
#define PHV_OFF_FLOW_HASH           260

// ─────────────────────────────────────────────
// 扩展表：ARP Punt / VLAN / DSCP→优先级
// ─────────────────────────────────────────────

// ARP Punt 表（stage 3）：ethertype=0x0806 → punt
#define TABLE_ARP_TRAP_STAGE        3
#define TABLE_ARP_TRAP_BASE         0x0000

// VLAN 入口分类表（stage 4）：(ing_port, vlan_tci) → 接受/打标/丢弃
#define TABLE_VLAN_INGRESS_STAGE    4
#define TABLE_VLAN_INGRESS_BASE     0x0000

// DSCP → 优先级映射表（stage 5）：ipv4_dscp → qos_prio
#define TABLE_DSCP_MAP_STAGE        5
#define TABLE_DSCP_MAP_BASE         0x0000

// VLAN 出口标签处理表（stage 6）：(eg_port, vlan_id) → 保留/剥离标签
#define TABLE_VLAN_EGRESS_STAGE     6
#define TABLE_VLAN_EGRESS_BASE      0x0000

// ─────────────────────────────────────────────
// Action ID 扩展
// ─────────────────────────────────────────────

// ARP Punt
#define ACTION_PUNT_CPU             0x4001   // 复制报头到 CPU Punt 环

// VLAN 入口
#define ACTION_VLAN_ASSIGN_PVID     0x5001   // 无标签帧 → 赋 PVID
#define ACTION_VLAN_ACCEPT_TAGGED   0x5002   // 带标签帧 → 接受并记录 vlan_id
#define ACTION_VLAN_DROP            0x5003   // VLAN 不匹配 → 丢弃

// VLAN 出口
#define ACTION_VLAN_STRIP_TAG       0x5004   // access 出口：剥离 VLAN 标签
#define ACTION_VLAN_KEEP_TAG        0x5005   // trunk 出口：保留 VLAN 标签

// DSCP → 队列优先级
#define ACTION_SET_PRIO             0x6001   // set meta.qos_prio = param[0]

// ─────────────────────────────────────────────
// 计数器 / Meter ID
// ─────────────────────────────────────────────
#define COUNTER_IPV4_LPM_HITS       0x0000
#define COUNTER_ACL_DENY            0x0001
#define COUNTER_L2_FDB_HITS         0x0002
#define COUNTER_ARP_PUNT            0x0010
#define COUNTER_VLAN_DROP           0x0011
#define COUNTER_DSCP_REMARK         0x0012

#define METER_PORT_BASE             0x0100   // per-port meter: METER_PORT_BASE + port

// ─────────────────────────────────────────────
// PHV 字段偏移扩展（元数据区）
// ─────────────────────────────────────────────
#define PHV_OFF_VLAN_ID             261      // 已解析 VLAN ID（uint16_t）
#define PHV_OFF_QOS_PRIO            263      // QoS 队列优先级（0-7）

#endif /* TABLE_MAP_H */
