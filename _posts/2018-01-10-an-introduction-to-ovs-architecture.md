---
layout: post
title: The introduction to OVS architecture
date: 2018-01-10 15:00:30
categories: Network
tags: OVS
excerpt: The introduction to OVS architecture
---

OVS的整体架构:

![](/assets/ovs/ovs_architecture_01.png)

## Architecture

`ovs-vswitchd`的整体架构如下：

```
           _
          |   +-------------------+
          |   |    ovs-vswitchd   |<-->ovsdb-server
          |   +-------------------+
          |   |      ofproto      |<-->OpenFlow controllers
          |   +--------+-+--------+  _
          |   | netdev | |ofproto-|   |
userspace |   +--------+ |  dpif  |   |
          |   | netdev | +--------+   |
          |   |provider| |  dpif  |   |
          |   +---||---+ +--------+   |
          |       ||     |  dpif  |   | implementation of
          |       ||     |provider|   | ofproto provider
          |_      ||     +---||---+   |
                  ||         ||       |
           _  +---||-----+---||---+   |
          |   |          |datapath|   |
   kernel |   |          +--------+  _|
          |   |                   |
          |_  +--------||---------+
                       ||
                    physical
                       NIC
```

* ovs-vswitchd

The main Open vSwitch userspace program, in vswitchd/. It reads the desired Open vSwitch configuration from the ovsdb-server program over an IPC channel and passes this configuration down to the “ofproto” library. It also passes certain status and statistical information from ofproto back into the database.

* ofproto
The Open vSwitch library, in ofproto/, that implements an OpenFlow switch. It talks to OpenFlow controllers over the network and to switch hardware or software through an “ofproto provider”, explained further below.

* netdev
The Open vSwitch library, in lib/netdev.c, that abstracts interacting with network devices, that is, Ethernet interfaces. The netdev library is a thin layer over “netdev provider” code, explained further below.


### ofproto

`struct ofproto`表示`An OpenFlow switch`:

```
///ofproto/ofproto-provider.h
/* An OpenFlow switch.
 *
 * With few exceptions, ofproto implementations may look at these fields but
 * should not modify them. */
struct ofproto {
    struct hmap_node hmap_node; /* In global 'all_ofprotos' hmap. */
    const struct ofproto_class *ofproto_class; /// see ofproto_dpif_class 
    char *type;                 /* Datapath type. */
    char *name;                 /* Datapath name. */
///...
    /* Datapath. */
    struct hmap ports;          /* Contains "struct ofport"s. */
    struct shash port_by_name;
    struct simap ofp_requests;  /* OpenFlow port number requests. */
    uint16_t alloc_port_no;     /* Last allocated OpenFlow port number. */
    uint16_t max_ports;         /* Max possible OpenFlow port num, plus one. */
    struct hmap ofport_usage;   /* Map ofport to last used time. */
    uint64_t change_seq;        /* Change sequence for netdev status. */

    /* Flow tables. */
    long long int eviction_group_timer; /* For rate limited reheapification. */
    struct oftable *tables;
    int n_tables;
    ovs_version_t tables_version;  /* Controls which rules are visible to
                                    * table lookups. */
///...
```

`struct ofproto`包含两个最重要的组成信息：端口信息（`struct ofport`）和流表信息(`struct oftable`):

* struct ofport

```
///ofproto/ofproto-provider.h
/* An OpenFlow port within a "struct ofproto".
 *
 * The port's name is netdev_get_name(port->netdev).
 *
 * With few exceptions, ofproto implementations may look at these fields but
 * should not modify them. */
struct ofport {
    struct hmap_node hmap_node; /* In struct ofproto's "ports" hmap. */
    struct ofproto *ofproto;    /* The ofproto that contains this port. */
    struct netdev *netdev; /// network device
    struct ofputil_phy_port pp;
    ofp_port_t ofp_port;        /* OpenFlow port number. */
    uint64_t change_seq;
    long long int created;      /* Time created, in msec. */
    int mtu;
};
```

* 流表

```
///ofproto/ofproto-provider.h
/* A flow table within a "struct ofproto".
*/
struct oftable {
    enum oftable_flags flags;
    struct classifier cls;      /* Contains "struct rule"s. */
    char *name;                 /* Table name exposed via OpenFlow, or NULL. */
////...
```

`struct oftable`通过`struct classifier`关联所有流表规则。

* 流表规则

```
///ofproto/ofproto-provider.h
struct rule {
    /* Where this rule resides in an OpenFlow switch.
     *
     * These are immutable once the rule is constructed, hence 'const'. */
    struct ofproto *const ofproto; /* The ofproto that contains this rule. */
    const struct cls_rule cr;      /* In owning ofproto's classifier. */
    const uint8_t table_id;        /* Index in ofproto's 'tables' array. */

    enum rule_state state;
///...
    /* OpenFlow actions.  See struct rule_actions for more thread-safety
     * notes. */
    const struct rule_actions * const actions;
///...
```

`struct rule`的`actions`为规则对应的action信息:

```
///ofproto/ofproto-provider.h
/* A set of actions within a "struct rule".
*/
struct rule_actions {
    /* Flags.
     *
     * 'has_meter' is true if 'ofpacts' contains an OFPACT_METER action.
     *
     * 'has_learn_with_delete' is true if 'ofpacts' contains an OFPACT_LEARN
     * action whose flags include NX_LEARN_F_DELETE_LEARNED. */
    bool has_meter;
    bool has_learn_with_delete;
    bool has_groups;

    /* Actions. */
    uint32_t ofpacts_len;         /* Size of 'ofpacts', in bytes. */
    struct ofpact ofpacts[];      /* Sequence of "struct ofpacts". */
};
```

* action

```
///include/openvswitch/ofp-action.sh
/* Header for an action.
 *
 * Each action is a structure "struct ofpact_*" that begins with "struct
 * ofpact", usually followed by other data that describes the action.  Actions
 * are padded out to a multiple of OFPACT_ALIGNTO bytes in length.
 */
struct ofpact {
    /* We want the space advantage of an 8-bit type here on every
     * implementation, without giving up the advantage of having a useful type
     * on implementations that support packed enums. */
#ifdef HAVE_PACKED_ENUM
    enum ofpact_type type;      /* OFPACT_*. */
#else
    uint8_t type;               /* OFPACT_* */
#endif

    uint8_t raw;                /* Original type when added, if any. */
    uint16_t len;               /* Length of the action, in bytes, including
                                 * struct ofpact, excluding padding. */
};
```

OFPACT_OUTPUT action:

```
/* OFPACT_OUTPUT.
 *
 * Used for OFPAT10_OUTPUT. */
struct ofpact_output {
    struct ofpact ofpact;
    ofp_port_t port;            /* Output port. */
    uint16_t max_len;           /* Max send len, for port OFPP_CONTROLLER. */
};
```

### ofproto_dpif


* struct ofproto_dpif

`struct ofproto_dpif`表示基于`dpif datapath`的bridge:

```
///ofproto/ofproto-dpif.h
/* A bridge based on a "dpif" datapath. */

struct ofproto_dpif {
    struct hmap_node all_ofproto_dpifs_node; /* In 'all_ofproto_dpifs'. */
    struct ofproto up;
    struct dpif_backer *backer;
///...
```

* struct dpif

基于`OVS`的`datapath interface`(`ofproto_dpif->backer->dpif`):

```
///ofproto/dpif-provider.h
/* Open vSwitch datapath interface.
 *
 * This structure should be treated as opaque by dpif implementations. */
struct dpif {
    const struct dpif_class *dpif_class;
    char *base_name;
    char *full_name;
    uint8_t netflow_engine_type;
    uint8_t netflow_engine_id;
};
```

`struct dpif_class`:

```
/* Datapath interface class structure, to be defined by each implementation of
 * a datapath interface.
 *
 * These functions return 0 if successful or a positive errno value on failure,
 * except where otherwise noted.
 *
 * These functions are expected to execute synchronously, that is, to block as
 * necessary to obtain a result.  Thus, they may not return EAGAIN or
 * EWOULDBLOCK or EINPROGRESS.  We may relax this requirement in the future if
 * and when we encounter performance problems. */
struct  { /// see dpif_netlink_class/dpif_netdev_class
    /* Type of dpif in this class, e.g. "system", "netdev", etc.
     *
     * One of the providers should supply a "system" type, since this is
     * the type assumed if no type is specified when opening a dpif. */
    const char *type;
 ///...
```

`struct dpif_class`实际上对应图中的`dpif provider`:

> The “dpif” library in turn delegates much of its functionality to a “dpif provider”. 
>
> `struct dpif_class`, in `lib/dpif-provider.h`, defines the interfaces required to implement a dpif provider for new hardware or software.
>
> There are two existing dpif implementations that may serve as useful examples during a port:
>
> * `lib/dpif-netlink.c` is a Linux-specific dpif implementation that talks to an Open vSwitch-specific kernel module (whose sources are in the “datapath” directory). The kernel module performs all of the switching work, passing packets that do not match any flow table entry up to userspace. This dpif implementation is essentially a wrapper around calls into the kernel module.
>
> * `lib/dpif-netdev.c` is a generic dpif implementation that performs all switching internally. This is how the Open vSwitch userspace switch is implemented.


* dpif_netdev_class

```
//lib/dpif_netdev.c
const struct dpif_class dpif_netdev_class = {
    "netdev",
    dpif_netdev_init,
///...
```

用户态switch实现，DPDK需要使用该类型：

```
ovs-vsctl add-br br0 -- set bridge br0 datapath_type=netdev
ovs-vsctl add-port br0 dpdk-p0 -- set Interface dpdk-p0 type=dpdk \
    options:dpdk-devargs=0000:01:00.0
```

参考[Using Open vSwitch with DPDK](http://docs.openvswitch.org/en/latest/howto/dpdk/).

* dpif_netlink_class

`system datapath`，基于Linux内核实现的switch:

```
//lib/dpif-netlink.c
const struct dpif_class dpif_netlink_class = {
    "system",
    NULL,                       /* init */
///...
```

### netdev

* struct netdev

`struct netdev`代表一个网络设备：

```
///lib/netdev-provider.h
/* A network device (e.g. an Ethernet device).
 *
 * Network device implementations may read these members but should not modify
 * them. */
struct netdev {
    /* The following do not change during the lifetime of a struct netdev. */
    char *name;                         /* Name of network device. */
    const struct netdev_class *netdev_class; /* Functions to control
                                                this device. */
///...
```

* struct netdev_class

```
const struct netdev_class netdev_linux_class =
    NETDEV_LINUX_CLASS(
        "system",
        netdev_linux_construct,
        netdev_linux_get_stats,
        netdev_linux_get_features,
        netdev_linux_get_status,
        LINUX_FLOW_OFFLOAD_API);

const struct netdev_class netdev_tap_class =
    NETDEV_LINUX_CLASS(
        "tap",
        netdev_linux_construct_tap,
        netdev_tap_get_stats,
        netdev_linux_get_features,
        netdev_linux_get_status,
        NO_OFFLOAD_API);

const struct netdev_class netdev_internal_class =
    NETDEV_LINUX_CLASS(
        "internal",
        netdev_linux_construct,
        netdev_internal_get_stats,
        NULL,                  /* get_features */
        netdev_internal_get_status,
        NO_OFFLOAD_API);
```


## ovs-vswitchd

`ovs-vswitchd`的基本功能包括bridge的维护、流表的维护和upcall处理等逻辑。

```
int
main(int argc, char *argv[])
{
///...
        bridge_run();
        unixctl_server_run(unixctl);
        netdev_run();
///...
```

* bridge_run

`bridge_run`根据从`ovsdb-server`读取的配置信息进行网桥建立、配置和更新，参考[OVS网桥建立和连接管理](https://www.sdnlab.com/16144.html):

```
bridge_run
|- bridge_init_ofproto ///Initialize the ofproto library
|  |- ofproto_init
|    |- ofproto_class->init
|
|- bridge_run__
|  |- ofproto_type_run
|  |- ofproto_run
|     |- ofproto_class->run
|     |- handle_openflow ///处理openflow message
|
|- bridge_reconfigure /// config bridge
```

* netdev_run

`netdev_run`会调用所有`netdev_class`的`run`回调函数：

```
/* Performs periodic work needed by all the various kinds of netdevs.
 *
 * If your program opens any netdevs, it must call this function within its
 * main poll loop. */
void
netdev_run(void)
    OVS_EXCLUDED(netdev_mutex)
{
    netdev_initialize();

    struct netdev_registered_class *rc;
    CMAP_FOR_EACH (rc, cmap_node, &netdev_classes) {
        if (rc->class->run) {
            rc->class->run(rc->class);//netdev_linux_class->netdev_linux_run
        }
    }
}
```

以`netdev_linux_class`为例，`netdev_linux_run`会通过netlink的sock得到虚拟网卡的状态，并且更新状态。


### add port

```
///ofproto/ofproto.c
/* Attempts to add 'netdev' as a port on 'ofproto'.  If 'ofp_portp' is
 * non-null and '*ofp_portp' is not OFPP_NONE, attempts to use that as
 * the port's OpenFlow port number.
 *
 * If successful, returns 0 and sets '*ofp_portp' to the new port's
 * OpenFlow port number (if 'ofp_portp' is non-null).  On failure,
 * returns a positive errno value and sets '*ofp_portp' to OFPP_NONE (if
 * 'ofp_portp' is non-null). */
int
ofproto_port_add(struct ofproto *ofproto, struct netdev *netdev,
                 ofp_port_t *ofp_portp)
{
    ofp_port_t ofp_port = ofp_portp ? *ofp_portp : OFPP_NONE;
    int error;

    error = ofproto->ofproto_class->port_add(ofproto, netdev); ///ofproto_dpif_class(->port_add)
///...
```

* dpif add port

`port_add` -> `dpif_port_add`:

```
///lib/dpif.c
int
dpif_port_add(struct dpif *dpif, struct netdev *netdev, odp_port_t *port_nop)
{
    const char *netdev_name = netdev_get_name(netdev);
    odp_port_t port_no = ODPP_NONE;
    int error;

    COVERAGE_INC(dpif_port_add);

    if (port_nop) {
        port_no = *port_nop;
    }

    error = dpif->dpif_class->port_add(dpif, netdev, &port_no); ///dpif_netlink_class
///...
```

* dpif-netlink

`lib/dpif-netlink.c`: `dpif_netlink_port_add` -> `dpif_netlink_port_add_compat` -> `dpif_netlink_port_add__`:

```
static int
dpif_netlink_port_add__(struct dpif_netlink *dpif, const char *name,
                        enum ovs_vport_type type,
                        struct ofpbuf *options,
                        odp_port_t *port_nop)
    OVS_REQ_WRLOCK(dpif->upcall_lock)
{
///...
    dpif_netlink_vport_init(&request);
    request.cmd = OVS_VPORT_CMD_NEW; ///new port
    request.dp_ifindex = dpif->dp_ifindex;
    request.type = type;
    request.name = name;

    request.port_no = *port_nop;
    upcall_pids = vport_socksp_to_pids(socksp, dpif->n_handlers);
    request.n_upcall_pids = socksp ? dpif->n_handlers : 1;
    request.upcall_pids = upcall_pids;

    if (options) {
        request.options = options->data;
        request.options_len = options->size;
    }

    error = dpif_netlink_vport_transact(&request, &reply, &buf);
///...
```

* data path

```
# ovs-vsctl add-port br-int vxlan1 -- set interface vxlan1 type=vxlan options:remote_ip=172.18.42.161
```

`datapath`函数调用:

```
ovs_vport_cmd_new
|-- new_vport
    |-- ovs_vport_add
        |-- vport_ops->create
```


### add flow

```
ovs-ofctl add-flow br-int "in_port=2, nw_src=192.168.1.100, action=drop"
```

`handle_openflow` -> `handle_openflow__`  -> `handle_flow_mod` -> `handle_flow_mod__` -> `ofproto_flow_mod_start`  -> `add_flow_start` -> `replace_rule_start` -> `ofproto_rule_insert__`.

参考[Openvswitch原理与代码分析(7): 添加一条流表flow](http://www.cnblogs.com/popsuper1982/p/5904329.html).

### upcall

无论是内核态datapath还是基于dpdk的用户态datapath，当flow table查不到之后都会进入upcall的处理. upcall的处理函数`udpif_upcall_handler`会在`udpif_start_threads`里面初始化，同时创建的还有`udpif_revalidator`的线程:

`recv_upcalls`是upcall处理的入口函数：

```
recv_upcalls
|-- dpif_recv         // (1) read packet from datapath
|-- upcall_receive    // (2) associate packet with a ofproto
|-- process_upcall    // (3) process packet by flow rule
|-- handle_upcalls    // (4) add flow rule to datapath
```


* process_upcall

```
process_upcall
|-- upcall_xlate
    |-- xlate_actions
        |-- rule_dpif_lookup_from_table
        |-- do_xlate_actions
```


* do_xlate_actions

```
static void
do_xlate_actions(const struct ofpact *ofpacts, size_t ofpacts_len,
                 struct xlate_ctx *ctx, bool is_last_action)
{
    struct flow_wildcards *wc = ctx->wc;
    struct flow *flow = &ctx->xin->flow;
    const struct ofpact *a;
///...
    ///do each action
    OFPACT_FOR_EACH (a, ofpacts, ofpacts_len) {
///...
        switch (a->type) {
        case OFPACT_OUTPUT: /// actions=output
            xlate_output_action(ctx, ofpact_get_OUTPUT(a)->port,
                                ofpact_get_OUTPUT(a)->max_len, true, last,
                                false);
            break;

        case OFPACT_GROUP: /// actions=group
            if (xlate_group_action(ctx, ofpact_get_GROUP(a)->group_id, last)) {
                /* Group could not be found. */

                /* XXX: Terminates action list translation, but does not
                 * terminate the pipeline. */
                return;
            }
            break;
///...
        case OFPACT_CT: /// actions=ct
            compose_conntrack_action(ctx, ofpact_get_CT(a), last);
            break;
```

## Refs

* [Porting Open vSwitch to New Software or Hardware](http://docs.openvswitch.org/en/latest/topics/porting/)
* [OVS网桥建立和连接管理](https://www.sdnlab.com/16144.html)
* [Openvswitch原理与代码分析(1)：总体架构](http://www.cnblogs.com/popsuper1982/p/5848879.html)
