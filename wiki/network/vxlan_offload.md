
## VXLAN device

VXLAN设备默认开启`TSO/UFO/GRO`:

```
# ./ethtool -k flannel.1 
Features for flannel.1:
rx-checksumming: on
tx-checksumming: on
        tx-checksum-ipv4: off [fixed]
        tx-checksum-ip-generic: on
        tx-checksum-ipv6: off [fixed]
        tx-checksum-fcoe-crc: off [fixed]
        tx-checksum-sctp: off [fixed]
scatter-gather: on
        tx-scatter-gather: on
        tx-scatter-gather-fraglist: off [fixed]
tcp-segmentation-offload: on
        tx-tcp-segmentation: on
        tx-tcp-ecn-segmentation: on
        tx-tcp6-segmentation: on
udp-fragmentation-offload: on
generic-segmentation-offload: on
generic-receive-offload: on
large-receive-offload: off [fixed]
rx-vlan-offload: off [fixed]
tx-vlan-offload: off [fixed]
ntuple-filters: off [fixed]
receive-hashing: off [fixed]
highdma: off [fixed]
rx-vlan-filter: off [fixed]
vlan-challenged: off [fixed]
tx-lockless: on [fixed]
netns-local: on [fixed]
tx-gso-robust: off [fixed]
tx-fcoe-segmentation: off [fixed]
tx-gre-segmentation: off [fixed]
tx-udp_tnl-segmentation: off [fixed]
fcoe-mtu: off [fixed]
tx-nocache-copy: on
loopback: off [fixed]
rx-fcs: off [fixed]
rx-all: off [fixed]
tx-vlan-stag-hw-insert: off [fixed]
rx-vlan-stag-hw-parse: off [fixed]
rx-vlan-stag-filter: off [fixed]
```

```
/* Initialize the device structure. */
static void vxlan_setup(struct net_device *dev)
{
///...
	dev->tx_queue_len = 0;
	dev->features	|= NETIF_F_LLTX;
	dev->features	|= NETIF_F_NETNS_LOCAL;
	dev->features	|= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->features   |= NETIF_F_RXCSUM;
	dev->features   |= NETIF_F_GSO_SOFTWARE;

	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;
	dev->priv_flags	&= ~IFF_XMIT_DST_RELEASE;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
}


/* List of features with software fallbacks. */
#define NETIF_F_GSO_SOFTWARE	(NETIF_F_TSO | NETIF_F_TSO_ECN | \
				 NETIF_F_TSO6 | NETIF_F_UFO)
```


## X722 device

### 关闭eth1的`vxlan offload`

```
# ./ethtool -K eth1 tx-udp_tnl-segmentation off
```

`vxlan_xmit` -> `dev_hard_start_xmit` -> `__skb_gso_segment` -> `udp4_ufo_fragment(外层vxlan包)` -> `tcp_tso_segment(内层tcp包)`:

```
          <idle>-0     [027] d.s. 717578.788817: tcp_tso_segment: (tcp_tso_segment+0x0/0x3f0)
          <idle>-0     [027] d.s. 717578.788821: <stack trace>
 => skb_mac_gso_segment
 => udp4_ufo_fragment
 => inet_gso_segment
 => skb_mac_gso_segment
 => __skb_gso_segment
 => dev_hard_start_xmit
 => sch_direct_xmit
 => dev_queue_xmit
 => ip_finish_output
 => ip_output
 => ip_local_out
 => vxlan_xmit_one
 => vxlan_xmit
 => dev_hard_start_xmit
 => dev_queue_xmit
 => ip_finish_output
 => ip_output
 => ip_local_out
 => ip_queue_xmit
 => tcp_transmit_skb
 => tcp_write_xmit
 => __tcp_push_pending_frames
 => tcp_rcv_established
 => tcp_v4_do_rcv
 => tcp_v4_rcv
 => ip_local_deliver_finish
 => ip_local_deliver
 => ip_rcv_finish
 => ip_rcv
 => __netif_receive_skb_core
 => __netif_receive_skb
 => process_backlog
 => net_rx_action
 => __do_softirq
 => call_softirq
 => do_softirq
 => irq_exit
 => do_IRQ
 => ret_from_intr
 => cpuidle_idle_call
 => arch_cpu_idle
 => cpu_startup_entry
 => start_secondary
 ```
 
 * udp4_ufo_fragment
 
 ```
  12)               |  udp4_ufo_fragment() {
  12)               |    netif_skb_dev_features() {
  12)   0.026 us    |      harmonize_features.isra.53.part.54();
  12)   0.197 us    |    }
  12)               |    skb_mac_gso_segment() {
  12)   0.022 us    |      skb_network_protocol();
  12)               |      inet_gso_segment() {
  12)               |        tcp_tso_segment() {
  12)   1.034 us    |          skb_segment();
  12)   0.035 us    |          csum_partial();
  12)   0.036 us    |          csum_partial();
  12)   1.608 us    |        }
  12)   1.797 us    |      }
  12)   2.144 us    |    }
  12)   0.022 us    |    skb_push();
  12)   0.021 us    |    skb_push();
  12)   3.042 us    |  }
```


### 打开eth1的`vxlan offload`

```
# ./ethtool -K eth1 tx-udp_tnl-segmentation on
```

vxlan设备发送数据时，不再做`software offload`，即不再调用`__skb_gso_segment`:

```
# ./ethtool -K eth1 tx-udp_tnl-segmentation on
```


```
  17)               |  vxlan_xmit_one [vxlan]() {
  17)               |    __skb_get_rxhash() {
  17)   0.034 us    |      skb_flow_dissect();
  17)   0.205 us    |    }
  17)               |    ip_route_output_flow() {
  17)               |      __ip_route_output_key() {
  17)   0.031 us    |        __ip_dev_find();
  17)   0.034 us    |        fib_table_lookup();
  17)               |        fib_table_lookup() {
  17)   0.022 us    |          check_leaf.isra.7();
  17)   0.030 us    |          check_leaf.isra.7();
  17)   0.412 us    |        }
  17)   0.973 us    |      }
  17)   1.138 us    |    }
  17)               |    __ip_select_ident() {
  17)   0.027 us    |      ip_idents_reserve();
  17)   0.191 us    |    }
  17)               |    tcp_wfree() {
  17)   0.022 us    |      sock_wfree();
  17)   0.190 us    |    }
  17)               |    ip_local_out() {
  17)               |      __ip_local_out() {
  17)               |        nf_hook_slow() {
  17)               |          nf_iterate() {
  17)   0.021 us    |            ipv4_conntrack_defrag [nf_defrag_ipv4]();
  17)               |            ipv4_conntrack_local [nf_conntrack_ipv4]() {
  17)               |              nf_conntrack_in [nf_conntrack]() {
  17)   0.024 us    |                ipv4_get_l4proto [nf_conntrack_ipv4]();
  17)   0.021 us    |                __nf_ct_l4proto_find [nf_conntrack]();
  17)   0.021 us    |                udp_error [nf_conntrack]();
  17)   0.069 us    |                nf_ct_get_tuple [nf_conntrack]();
  17)   0.027 us    |                hash_conntrack_raw [nf_conntrack]();
  17)   0.082 us    |                __nf_conntrack_find_get [nf_conntrack]();
  17)   0.020 us    |                udp_get_timeouts [nf_conntrack]();
  17)   0.034 us    |                udp_packet [nf_conntrack]();
  17)   1.566 us    |              }
  17)   1.731 us    |            }
  17)               |            nf_nat_ipv4_local_fn [iptable_nat]() {
  17)               |              nf_nat_ipv4_fn [iptable_nat]() {
  17)   0.022 us    |                nf_nat_packet [nf_nat]();
  17)   0.194 us    |              }
  17)   0.361 us    |            }
  17)               |            iptable_filter_hook [iptable_filter]() {
  17)               |              ipt_do_table [ip_tables]() {
  17)   0.023 us    |                local_bh_disable();
  17)   0.020 us    |                local_bh_enable();
  17)   0.470 us    |              }
  17)   0.634 us    |            }
  17)   3.433 us    |          }
  17)   3.602 us    |        }
  17)   3.767 us    |      }
  17)               |      ip_output() {
  17)               |        nf_hook_slow() {
  17)               |          nf_iterate() {
  17)               |            nf_nat_ipv4_out [iptable_nat]() {
  17)               |              nf_nat_ipv4_fn [iptable_nat]() {
  17)   0.022 us    |                nf_nat_packet [nf_nat]();
  17)   0.199 us    |              }
  17)   0.367 us    |            }
  17)   0.020 us    |            ipv4_helper [nf_conntrack_ipv4]();
  17)               |            ipv4_confirm [nf_conntrack_ipv4]() {
  17)   0.024 us    |              nf_ct_deliver_cached_events [nf_conntrack]();
  17)   0.201 us    |            }
  17)   1.082 us    |          }
  17)   1.248 us    |        }
  17)               |        ip_finish_output() {
  17)   0.019 us    |          ipv4_mtu();
  17)   0.019 us    |          local_bh_disable();
  17)   0.021 us    |          skb_push();
  17)               |          dev_queue_xmit() {
  17)   0.021 us    |            local_bh_disable();
  17)               |            netdev_pick_tx() {
  17)   0.029 us    |              __netdev_pick_tx();
  17)   0.198 us    |            }
  17)   0.020 us    |            _raw_spin_lock();
  17)               |            sch_direct_xmit() {
  17)   0.020 us    |              _raw_spin_lock();
  17)               |              dev_hard_start_xmit() {
  17)   0.022 us    |                dst_release();
  17)   0.033 us    |                netif_skb_dev_features();
  17)   0.271 us    |                dev_queue_xmit_nit();
  17)   0.119 us    |                i40e_lan_xmit_frame [i40e]();
  17)   1.180 us    |              }
  17)   0.020 us    |              _raw_spin_lock();
  17)   1.694 us    |            }
  17)   0.020 us    |            local_bh_enable();
  17)   2.741 us    |          }
  17)   0.020 us    |          local_bh_enable();
  17)   3.623 us    |        }
  17)   5.202 us    |      }
  17)   9.286 us    |    }
  17) + 11.828 us   |  }
  ```

## 总结

`vxlan`设备支持`TSO/GSO`,所以，内核的向`vxlan`设备发送数据时（调用`vxlan_xmit`）时，并不会做`software offload`。而在下发物理网卡时，如果下层物理网卡不支持硬件`offload`，内核做`VXLAN GSO`，然后调用物理网卡发送函数（`i40e_lan_xmit_frame`）。反之，则透传给物理网卡做`硬件offload`。但是，从网卡的流量统计`sar -n DEV`看不出这种区别。


## vxlan software offload

* VXLAN GSO(软件offload)

也就是说，不管怎样，`vxlan offload`都不是在`vxlan`这一层做的，而是下层物理网络设备层面做的（`software offload`/`hardware offload`）
如果下层物理网卡不支持`vxlan offload`，内核在下发物理网卡时，需要对UDP内层封包完成`软件offload`。

`udp4_ufo_fragment` -> `skb_udp_tunnel_segment`：
 
 ```
 ///udp_offload(software offload)
struct sk_buff *udp4_ufo_fragment(struct sk_buff *skb,
	netdev_features_t features)
{
///...
	/* Fragment the skb. IP headers of the fragments are updated in
	 * inet_gso_segment()
	 */
	if (skb->encapsulation && skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL)
		segs = skb_udp_tunnel_segment(skb, features); ///UDP tunnel packet
///...
```

* VXLAN GRO

[3.14](https://github.com/torvalds/linux/commit/dc01e7d3447793fd9e4090aa9d50c549848b5a18)支持`VXLAN GRO`:

```
/* Create new listen socket if needed */
static struct vxlan_sock *vxlan_socket_create(struct net *net, __be16 port,
					      u32 flags)
{
///...
	/* Initialize the vxlan udp offloads structure */
	vs->udp_offloads.port = port;
	vs->udp_offloads.callbacks.gro_receive  = vxlan_gro_receive;
	vs->udp_offloads.callbacks.gro_complete = vxlan_gro_complete;
}


/* Notify netdevs that UDP port started listening */
static void vxlan_notify_add_rx_port(struct vxlan_sock *vs)
{
	struct net_device *dev;
	struct sock *sk = vs->sock->sk;
	struct net *net = sock_net(sk);
	sa_family_t sa_family = vxlan_get_sk_family(vs);
	__be16 port = inet_sk(sk)->inet_sport;
	int err;

	if (sa_family == AF_INET) {
		err = udp_add_offload(&vs->udp_offloads);
		if (err)
			pr_warn("vxlan: udp_add_offload failed with status %d\n", err);
	}
///...
}
```



```
///udp_offload.c
struct sk_buff **udp_gro_receive(struct sk_buff **head, struct sk_buff *skb,
				 struct udphdr *uh)
{
	struct udp_offload_priv *uo_priv;
	struct sk_buff *p, **pp = NULL;
	struct udphdr *uh2;
	unsigned int off = skb_gro_offset(skb);
	int flush = 1;

	if (NAPI_GRO_CB(skb)->udp_mark ||
	    (skb->ip_summed != CHECKSUM_PARTIAL &&
	     NAPI_GRO_CB(skb)->csum_cnt == 0 &&
	     !NAPI_GRO_CB(skb)->csum_valid))
		goto out;

	/* mark that this skb passed once through the udp gro layer */
	NAPI_GRO_CB(skb)->udp_mark = 1;

	rcu_read_lock();
	uo_priv = rcu_dereference(udp_offload_base);
	for (; uo_priv != NULL; uo_priv = rcu_dereference(uo_priv->next)) {
		if (uo_priv->offload->port == uh->dest && ///same port (8472 for vxlan)
		    uo_priv->offload->callbacks.gro_receive)
			goto unflush;
	}
	goto out_unlock;

unflush:
	flush = 0;

	for (p = *head; p; p = p->next) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		uh2 = (struct udphdr   *)(p->data + off);

		/* Match ports and either checksums are either both zero
		 * or nonzero.
		 */
		if ((*(u32 *)&uh->source != *(u32 *)&uh2->source) ||
		    (!uh->check ^ !uh2->check)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	skb_gro_pull(skb, sizeof(struct udphdr)); /* pull encapsulating udp header */
	skb_gro_postpull_rcsum(skb, uh, sizeof(struct udphdr));
	NAPI_GRO_CB(skb)->proto = uo_priv->offload->ipproto;
	pp = uo_priv->offload->callbacks.gro_receive(head, skb, ///vxlan_gro_receive
						     uo_priv->offload);

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;
	return pp;
}
```
 
 参考 [【Linux4.1.12源码分析】协议栈gro收包之VXLAN处理](http://blog.csdn.net/one_clouder/article/details/53043390)
 
