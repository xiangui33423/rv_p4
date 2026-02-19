// cli_cmds.c
// CLI 命令实现
// 支持的命令：
//   help
//   show  vlan [<vid>] | arp | route | fdb | port [<port>] | qos [<port>] | acl
//   vlan  create <vid> | delete <vid>
//         port <vid> add <port> tagged|untagged
//         port <vid> remove <port>
//         pvid <port> <vid>
//   arp   add <ip> <mac> <port> [<vlan>]
//         del <ip>
//         probe <ip> <port> [<vlan>]
//   route add <ip/len> <port> <mac>
//         del <ip/len>
//   acl   deny <src/len> <dst/len> [<dport>]
//         permit <src/len> <dst/len>
//         del <rule_id>
//   qos   weight <port> <q0> <q1> <q2> <q3> <q4> <q5> <q6> <q7>
//         pir <port> <bps>
//         mode <port> dwrr|sp|sp+dwrr [<sp_queues>]
//         dscp <dscp_val> <queue>
//   port  enable <port> | disable <port>
//         stats [<port>]

#include "cli_cmds.h"
#include "vlan.h"
#include "arp.h"
#include "qos.h"
#include "fdb.h"
#include "route.h"
#include "acl.h"
#include "rv_p4_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ─────────────────────────────────────────────
// 解析工具
// ─────────────────────────────────────────────

/* "192.168.1.0" → uint32_t 大端 */
static int parse_ipv4(const char *s, uint32_t *out) {
    unsigned a, b, c, d;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255)       return -1;
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c <<  8) |  (uint32_t)d;
    return 0;
}

/* "192.168.1.0/24" → ip + len */
static int parse_prefix(const char *s, uint32_t *ip_out, uint8_t *len_out) {
    char buf[20];
    strncpy(buf, s, 19);
    buf[19] = '\0';
    char *slash = strchr(buf, '/');
    if (!slash) return -1;
    *slash = '\0';
    if (parse_ipv4(buf, ip_out) < 0) return -1;
    unsigned len;
    if (sscanf(slash + 1, "%u", &len) != 1 || len > 32) return -1;
    *len_out = (uint8_t)len;
    return 0;
}

/* "aa:bb:cc:dd:ee:ff" → uint64_t */
static int parse_mac(const char *s, uint64_t *out) {
    unsigned a, b, c, d, e, f;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f) != 6)
        return -1;
    if (a > 0xFF || b > 0xFF || c > 0xFF || d > 0xFF || e > 0xFF || f > 0xFF)
        return -1;
    *out = ((uint64_t)a << 40) | ((uint64_t)b << 32) | ((uint64_t)c << 24) |
           ((uint64_t)d << 16) | ((uint64_t)e <<  8) |  (uint64_t)f;
    return 0;
}

/* MAC → 字节数组（6B） */
static int parse_mac_bytes(const char *s, uint8_t mac[6]) {
    uint64_t v;
    if (parse_mac(s, &v) < 0) return -1;
    mac[0] = (uint8_t)((v >> 40) & 0xFF);
    mac[1] = (uint8_t)((v >> 32) & 0xFF);
    mac[2] = (uint8_t)((v >> 24) & 0xFF);
    mac[3] = (uint8_t)((v >> 16) & 0xFF);
    mac[4] = (uint8_t)((v >>  8) & 0xFF);
    mac[5] = (uint8_t)((v >>  0) & 0xFF);
    return 0;
}

/* 十进制/十六进制 → uint32 */
static int parse_u32(const char *s, uint32_t *out) {
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (*end != '\0') return -1;
    *out = (uint32_t)v;
    return 0;
}

/* 打印端口统计 */
static void print_port_stats_one(uint8_t port) {
    port_stats_t s;
    if (hal_port_stats((port_id_t)port, &s) == HAL_OK) {
        printf("Port%2d: rx=%llu pkts/%llu B  tx=%llu pkts/%llu B\n",
               port,
               (unsigned long long)s.rx_pkts,  (unsigned long long)s.rx_bytes,
               (unsigned long long)s.tx_pkts,  (unsigned long long)s.tx_bytes);
    }
}

// ─────────────────────────────────────────────
// show
// ─────────────────────────────────────────────

static int cmd_show(int argc, char **argv) {
    if (argc < 2) {
        printf("show: need subcommand (vlan|arp|route|fdb|port|qos|acl)\n");
        return 1;
    }
    if (strcmp(argv[1], "vlan") == 0) {
        vlan_show();
    } else if (strcmp(argv[1], "arp") == 0) {
        arp_show();
    } else if (strcmp(argv[1], "route") == 0) {
        route_show();
    } else if (strcmp(argv[1], "fdb") == 0) {
        fdb_show();
    } else if (strcmp(argv[1], "acl") == 0) {
        acl_show();
    } else if (strcmp(argv[1], "qos") == 0) {
        if (argc >= 3) {
            uint32_t port;
            if (parse_u32(argv[2], &port) < 0 || port >= 32) {
                printf("show qos: bad port\n");
            } else {
                qos_show_port((port_id_t)port);
            }
        } else {
            for (int p = 0; p < 32; p++)
                qos_show_port((port_id_t)p);
        }
    } else if (strcmp(argv[1], "port") == 0) {
        if (argc >= 3) {
            uint32_t port;
            if (parse_u32(argv[2], &port) < 0 || port >= 32) {
                printf("show port: bad port\n");
            } else {
                print_port_stats_one((uint8_t)port);
            }
        } else {
            for (int p = 0; p < 32; p++)
                print_port_stats_one((uint8_t)p);
        }
    } else {
        printf("show: unknown subcommand '%s'\n", argv[1]);
    }
    return 1;
}

// ─────────────────────────────────────────────
// vlan
// ─────────────────────────────────────────────

static int cmd_vlan(int argc, char **argv) {
    if (argc < 2) goto vlan_usage;

    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) goto vlan_usage;
        uint32_t vid;
        if (parse_u32(argv[2], &vid) < 0 || vid < 1 || vid > 255) {
            printf("vlan create: bad vlan-id (1-255)\n"); return 1;
        }
        int r = vlan_create((uint16_t)vid);
        if (r == HAL_OK) printf("VLAN %u created\n", vid);
        else             printf("vlan create failed: %d\n", r);

    } else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) goto vlan_usage;
        uint32_t vid;
        if (parse_u32(argv[2], &vid) < 0 || vid < 1 || vid > 255) {
            printf("vlan delete: bad vlan-id\n"); return 1;
        }
        int r = vlan_delete((uint16_t)vid);
        if (r == HAL_OK) printf("VLAN %u deleted\n", vid);
        else             printf("vlan delete failed: %d\n", r);

    } else if (strcmp(argv[1], "port") == 0) {
        /* vlan port <vid> add|remove <port> [tagged|untagged] */
        if (argc < 5) goto vlan_usage;
        uint32_t vid, port;
        if (parse_u32(argv[2], &vid)  < 0 || vid < 1 || vid > 255 ||
            parse_u32(argv[4], &port) < 0 || port >= 32) {
            printf("vlan port: bad vlan-id or port\n"); return 1;
        }
        if (strcmp(argv[3], "add") == 0) {
            uint8_t tagged = 0;
            if (argc >= 6) {
                if (strcmp(argv[5], "tagged") == 0)   tagged = 1;
                else if (strcmp(argv[5], "untagged") == 0) tagged = 0;
                else { printf("vlan port add: expected tagged|untagged\n"); return 1; }
            }
            int r = vlan_port_add((uint16_t)vid, (port_id_t)port, tagged);
            if (r == HAL_OK) printf("Port %u %s VLAN %u\n",
                                    port, tagged ? "tagged" : "untagged", vid);
            else             printf("vlan port add failed: %d\n", r);
        } else if (strcmp(argv[3], "remove") == 0) {
            int r = vlan_port_remove((uint16_t)vid, (port_id_t)port);
            if (r == HAL_OK) printf("Port %u removed from VLAN %u\n", port, vid);
            else             printf("vlan port remove failed: %d\n", r);
        } else {
            goto vlan_usage;
        }

    } else if (strcmp(argv[1], "pvid") == 0) {
        if (argc < 4) goto vlan_usage;
        uint32_t port, vid;
        if (parse_u32(argv[2], &port) < 0 || port >= 32 ||
            parse_u32(argv[3], &vid)  < 0 || vid < 1 || vid > 255) {
            printf("vlan pvid: bad port or vlan-id\n"); return 1;
        }
        int r = vlan_port_set_pvid((port_id_t)port, (uint16_t)vid);
        if (r == HAL_OK) printf("Port %u PVID set to %u\n", port, vid);
        else             printf("vlan pvid failed: %d\n", r);
    } else {
        goto vlan_usage;
    }
    return 1;

vlan_usage:
    printf("Usage:\n"
           "  vlan create <vid>\n"
           "  vlan delete <vid>\n"
           "  vlan port <vid> add <port> [tagged|untagged]\n"
           "  vlan port <vid> remove <port>\n"
           "  vlan pvid <port> <vid>\n");
    return 1;
}

// ─────────────────────────────────────────────
// arp
// ─────────────────────────────────────────────

static int cmd_arp(int argc, char **argv) {
    if (argc < 2) goto arp_usage;

    if (strcmp(argv[1], "add") == 0) {
        /* arp add <ip> <mac> <port> [<vlan>] */
        if (argc < 5) goto arp_usage;
        uint32_t ip, port;
        uint8_t  mac[6];
        if (parse_ipv4(argv[2], &ip)        < 0 ||
            parse_mac_bytes(argv[3], mac)   < 0 ||
            parse_u32(argv[4], &port)       < 0 || port >= 32) {
            printf("arp add: parse error\n"); return 1;
        }
        uint32_t vlan = 0;
        if (argc >= 6) parse_u32(argv[5], &vlan);
        int r = arp_add(ip, mac, (port_id_t)port, (uint16_t)vlan);
        if (r == HAL_OK) printf("ARP entry added\n");
        else             printf("arp add failed: %d\n", r);

    } else if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) goto arp_usage;
        uint32_t ip;
        if (parse_ipv4(argv[2], &ip) < 0) { printf("arp del: bad ip\n"); return 1; }
        int r = arp_delete(ip);
        if (r == HAL_OK) printf("ARP entry deleted\n");
        else             printf("arp del failed: %d\n", r);

    } else if (strcmp(argv[1], "probe") == 0) {
        /* arp probe <ip> <port> [<vlan>] */
        if (argc < 4) goto arp_usage;
        uint32_t ip, port;
        if (parse_ipv4(argv[2], &ip)  < 0 ||
            parse_u32(argv[3], &port) < 0 || port >= 32) {
            printf("arp probe: parse error\n"); return 1;
        }
        uint32_t vlan = 0;
        if (argc >= 5) parse_u32(argv[4], &vlan);
        int r = arp_probe(ip, (port_id_t)port, (uint16_t)vlan);
        if (r == HAL_OK) printf("ARP probe sent\n");
        else             printf("arp probe failed: %d\n", r);
    } else {
        goto arp_usage;
    }
    return 1;

arp_usage:
    printf("Usage:\n"
           "  arp add <ip> <mac> <port> [<vlan>]\n"
           "  arp del <ip>\n"
           "  arp probe <ip> <port> [<vlan>]\n");
    return 1;
}

// ─────────────────────────────────────────────
// route
// ─────────────────────────────────────────────

static int cmd_route(int argc, char **argv) {
    if (argc < 2) goto route_usage;

    if (strcmp(argv[1], "add") == 0) {
        /* route add <ip/len> <port> <mac> */
        if (argc < 5) goto route_usage;
        uint32_t ip, port;
        uint8_t  len;
        uint64_t dmac;
        if (parse_prefix(argv[2], &ip, &len) < 0 ||
            parse_u32(argv[3], &port) < 0 || port >= 32 ||
            parse_mac(argv[4], &dmac) < 0) {
            printf("route add: parse error\n"); return 1;
        }
        int r = route_add(ip, len, (uint8_t)port, dmac);
        if (r == HAL_OK) printf("Route added\n");
        else             printf("route add failed: %d\n", r);

    } else if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) goto route_usage;
        uint32_t ip;
        uint8_t  len;
        if (parse_prefix(argv[2], &ip, &len) < 0) {
            printf("route del: bad prefix\n"); return 1;
        }
        int r = route_del(ip, len);
        if (r == HAL_OK) printf("Route deleted\n");
        else             printf("route del failed: %d\n", r);
    } else {
        goto route_usage;
    }
    return 1;

route_usage:
    printf("Usage:\n"
           "  route add <ip/len> <port> <mac>\n"
           "  route del <ip/len>\n");
    return 1;
}

// ─────────────────────────────────────────────
// acl
// ─────────────────────────────────────────────

static int cmd_acl(int argc, char **argv) {
    if (argc < 2) goto acl_usage;

    if (strcmp(argv[1], "deny") == 0) {
        /* acl deny <src/len> <dst/len> [<dport>] */
        if (argc < 4) goto acl_usage;
        uint32_t src, dst;
        uint8_t  slen, dlen;
        if (parse_prefix(argv[2], &src, &slen) < 0 ||
            parse_prefix(argv[3], &dst, &dlen) < 0) {
            printf("acl deny: bad prefix\n"); return 1;
        }
        uint32_t smask = slen ? ~((1U << (32 - slen)) - 1U) : 0U;
        uint32_t dmask = dlen ? ~((1U << (32 - dlen)) - 1U) : 0U;
        uint32_t dport = 0;
        if (argc >= 5) parse_u32(argv[4], &dport);
        int r = acl_add_deny(src, smask, dst, dmask, (uint16_t)dport);
        if (r >= 0) printf("ACL deny rule added (id=%d)\n", r);
        else        printf("acl deny failed: %d\n", r);

    } else if (strcmp(argv[1], "permit") == 0) {
        /* acl permit <src/len> <dst/len> */
        if (argc < 4) goto acl_usage;
        uint32_t src, dst;
        uint8_t  slen, dlen;
        if (parse_prefix(argv[2], &src, &slen) < 0 ||
            parse_prefix(argv[3], &dst, &dlen) < 0) {
            printf("acl permit: bad prefix\n"); return 1;
        }
        uint32_t smask = slen ? ~((1U << (32 - slen)) - 1U) : 0U;
        uint32_t dmask = dlen ? ~((1U << (32 - dlen)) - 1U) : 0U;
        int r = acl_add_permit(src, smask, dst, dmask);
        if (r >= 0) printf("ACL permit rule added (id=%d)\n", r);
        else        printf("acl permit failed: %d\n", r);

    } else if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) goto acl_usage;
        uint32_t id;
        if (parse_u32(argv[2], &id) < 0) { printf("acl del: bad id\n"); return 1; }
        int r = acl_delete((uint16_t)id);
        if (r == HAL_OK) printf("ACL rule %u deleted\n", id);
        else             printf("acl del failed: %d\n", r);
    } else {
        goto acl_usage;
    }
    return 1;

acl_usage:
    printf("Usage:\n"
           "  acl deny <src/len> <dst/len> [<dport>]\n"
           "  acl permit <src/len> <dst/len>\n"
           "  acl del <rule_id>\n");
    return 1;
}

// ─────────────────────────────────────────────
// qos
// ─────────────────────────────────────────────

static int cmd_qos(int argc, char **argv) {
    if (argc < 2) goto qos_usage;

    if (strcmp(argv[1], "weight") == 0) {
        /* qos weight <port> <q0>..<q7> */
        if (argc < 11) goto qos_usage;
        uint32_t port;
        if (parse_u32(argv[2], &port) < 0 || port >= 32) {
            printf("qos weight: bad port\n"); return 1;
        }
        uint32_t w[8];
        for (int i = 0; i < 8; i++) {
            if (parse_u32(argv[3 + i], &w[i]) < 0) {
                printf("qos weight: bad weight[%d]\n", i); return 1;
            }
        }
        int r = qos_port_set_weights((port_id_t)port, w);
        if (r == HAL_OK) printf("QoS weights set for port %u\n", port);
        else             printf("qos weight failed: %d\n", r);

    } else if (strcmp(argv[1], "pir") == 0) {
        /* qos pir <port> <bps> */
        if (argc < 4) goto qos_usage;
        uint32_t port;
        if (parse_u32(argv[2], &port) < 0 || port >= 32) {
            printf("qos pir: bad port\n"); return 1;
        }
        char *end;
        unsigned long long bps = strtoull(argv[3], &end, 0);
        if (*end != '\0') { printf("qos pir: bad bps\n"); return 1; }
        int r = qos_port_set_pir((port_id_t)port, (uint64_t)bps);
        if (r == HAL_OK) printf("QoS PIR set: port %u = %llu bps\n",
                                port, (unsigned long long)bps);
        else             printf("qos pir failed: %d\n", r);

    } else if (strcmp(argv[1], "mode") == 0) {
        /* qos mode <port> dwrr|sp|sp+dwrr [<sp_queues>] */
        if (argc < 4) goto qos_usage;
        uint32_t port;
        if (parse_u32(argv[2], &port) < 0 || port >= 32) {
            printf("qos mode: bad port\n"); return 1;
        }
        uint8_t mode;
        if      (strcmp(argv[3], "dwrr")     == 0) mode = QOS_SCHED_DWRR;
        else if (strcmp(argv[3], "sp")       == 0) mode = QOS_SCHED_SP;
        else if (strcmp(argv[3], "sp+dwrr")  == 0) mode = QOS_SCHED_SP_DWRR;
        else { printf("qos mode: expected dwrr|sp|sp+dwrr\n"); return 1; }
        uint32_t spq = 0;
        if (argc >= 5) parse_u32(argv[4], &spq);
        int r = qos_port_set_mode((port_id_t)port, mode, (uint8_t)spq);
        if (r == HAL_OK) printf("QoS mode set for port %u\n", port);
        else             printf("qos mode failed: %d\n", r);

    } else if (strcmp(argv[1], "dscp") == 0) {
        /* qos dscp <dscp_val> <queue> */
        if (argc < 4) goto qos_usage;
        uint32_t dscp, queue;
        if (parse_u32(argv[2], &dscp)  < 0 || dscp  >= 64 ||
            parse_u32(argv[3], &queue) < 0 || queue >= 8) {
            printf("qos dscp: dscp 0-63, queue 0-7\n"); return 1;
        }
        int r = qos_dscp_set((uint8_t)dscp, (uint8_t)queue);
        if (r == HAL_OK) printf("DSCP %u → Queue %u\n", dscp, queue);
        else             printf("qos dscp failed: %d\n", r);
    } else {
        goto qos_usage;
    }
    return 1;

qos_usage:
    printf("Usage:\n"
           "  qos weight <port> <q0> <q1> <q2> <q3> <q4> <q5> <q6> <q7>\n"
           "  qos pir <port> <bps>\n"
           "  qos mode <port> dwrr|sp|sp+dwrr [<sp_queues>]\n"
           "  qos dscp <dscp_val> <queue>\n");
    return 1;
}

// ─────────────────────────────────────────────
// port
// ─────────────────────────────────────────────

static int cmd_port(int argc, char **argv) {
    if (argc < 2) goto port_usage;

    if (strcmp(argv[1], "enable") == 0) {
        if (argc < 3) goto port_usage;
        uint32_t port;
        if (parse_u32(argv[2], &port) < 0 || port >= 32) {
            printf("port enable: bad port\n"); return 1;
        }
        hal_port_enable((port_id_t)port);
        printf("Port %u enabled\n", port);

    } else if (strcmp(argv[1], "disable") == 0) {
        if (argc < 3) goto port_usage;
        uint32_t port;
        if (parse_u32(argv[2], &port) < 0 || port >= 32) {
            printf("port disable: bad port\n"); return 1;
        }
        hal_port_disable((port_id_t)port);
        printf("Port %u disabled\n", port);

    } else if (strcmp(argv[1], "stats") == 0) {
        if (argc >= 3) {
            uint32_t port;
            if (parse_u32(argv[2], &port) < 0 || port >= 32) {
                printf("port stats: bad port\n"); return 1;
            }
            print_port_stats_one((uint8_t)port);
        } else {
            for (int p = 0; p < 32; p++)
                print_port_stats_one((uint8_t)p);
        }
    } else {
        goto port_usage;
    }
    return 1;

port_usage:
    printf("Usage:\n"
           "  port enable <port>\n"
           "  port disable <port>\n"
           "  port stats [<port>]\n");
    return 1;
}

// ─────────────────────────────────────────────
// help
// ─────────────────────────────────────────────

static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    printf(
        "RV-P4 Control Plane CLI Commands\n"
        "─────────────────────────────────────────────────────\n"
        "show   vlan [<vid>] | arp | route | fdb | port [<p>]\n"
        "       qos [<port>] | acl\n"
        "vlan   create <vid> | delete <vid>\n"
        "       port <vid> add|remove <port> [tagged|untagged]\n"
        "       pvid <port> <vid>\n"
        "arp    add <ip> <mac> <port> [<vlan>]\n"
        "       del <ip> | probe <ip> <port> [<vlan>]\n"
        "route  add <ip/len> <port> <mac> | del <ip/len>\n"
        "acl    deny <src/len> <dst/len> [<dport>]\n"
        "       permit <src/len> <dst/len> | del <rule_id>\n"
        "qos    weight <port> <q0..q7>\n"
        "       pir <port> <bps> | dscp <val> <queue>\n"
        "       mode <port> dwrr|sp|sp+dwrr [<sp_queues>]\n"
        "port   enable|disable <port> | stats [<port>]\n"
        "help\n");
    return 1;
}

// ─────────────────────────────────────────────
// 分发表
// ─────────────────────────────────────────────

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
} cmd_t;

static const cmd_t cmd_table[] = {
    { "show",  cmd_show  },
    { "vlan",  cmd_vlan  },
    { "arp",   cmd_arp   },
    { "route", cmd_route },
    { "acl",   cmd_acl   },
    { "qos",   cmd_qos   },
    { "port",  cmd_port  },
    { "help",  cmd_help  },
    { NULL,    NULL      },
};

int cli_exec_cmd(int argc, char **argv) {
    if (argc == 0) return 0;
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0)
            return cmd_table[i].fn(argc, argv);
    }
    return 0;   /* 未知命令 */
}
