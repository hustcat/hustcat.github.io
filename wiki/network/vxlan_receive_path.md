
* `ixgbe` -> `vxlan`

```
# ./kprobe -s 'p:vxlan_udp_encap_recv'
vxlan_udp_encap_recv [vxlan]
Tracing kprobe vxlan_udp_encap_recv. Ctrl-C to end.
          <idle>-0     [012] d.s. 9005615.748751: vxlan_udp_encap_recv: (vxlan_udp_encap_recv+0x0/0x4c0 [vxlan])
          <idle>-0     [012] d.s. 9005615.748762: <stack trace>
 => __udp4_lib_rcv
 => udp_rcv
 => ip_local_deliver_finish
 => ip_local_deliver
 => ip_rcv_finish
 => ip_rcv
 => __netif_receive_skb_core
 => __netif_receive_skb
 => netif_receive_skb
 => napi_gro_receive
 => ixgbe_clean_rx_irq
 => ixgbe_poll
 => net_rx_action
 => __do_softirq
 => call_softirq
 => do_softirq
 => irq_exit
 => do_IRQ
 => ret_from_intr
 => cpuidle_idle_call
 => arch_cpu_idle
 => cpu_idle_loop
 => cpu_startup_entry
 => start_secondary
 ```
 
 * NET_RX_SOFTIRQ
 
 ```
 # ./funcgraph -m 8 'vxlan_udp_encap_recv' 
Tracing "vxlan_udp_encap_recv"... Ctrl-C to end.
 12)               |  vxlan_udp_encap_recv [vxlan]() {
 12)   0.505 us    |    vxlan_find_vni [vxlan]();
 12)   0.255 us    |    eth_type_trans();
 12)               |    netif_rx() {
 12)               |      enqueue_to_backlog() {
 12)   0.119 us    |        _raw_spin_lock();
 12)   0.036 us    |        __raise_softirq_irqoff();
 12)   0.829 us    |      }
 12)   1.286 us    |    }
 12)   3.821 us    |  }
 ```
 
 * `vxlan` -> `bridge`

```
# ./kprobe -s 'p:br_dev_xmit'
br_dev_xmit
Tracing kprobe br_dev_xmit. Ctrl-C to end.
          <idle>-0     [012] d.s. 9005405.052345: br_dev_xmit: (br_dev_xmit+0x0/0x1d0)
          <idle>-0     [012] d.s. 9005405.052352: <stack trace>
 => dev_queue_xmit
 => ip_finish_output
 => ip_output
 => ip_forward_finish
 => ip_forward
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
 => cpu_idle_loop
 => cpu_startup_entry
 => start_secondary
 ```
 
 * with qdisc
 
 ```
 # ./kprobe -s 'p:br_dev_xmit' 
br_dev_xmit
Tracing kprobe br_dev_xmit. Ctrl-C to end.
          <idle>-0     [012] d.s. 9006194.364715: br_dev_xmit: (br_dev_xmit+0x0/0x1d0)
          <idle>-0     [012] d.s. 9006194.364723: <stack trace>
 => sch_direct_xmit
 => __qdisc_run
 => dev_queue_xmit
 => ip_finish_output
 => ip_output
 => ip_forward_finish
 => ip_forward
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
 => cpu_idle_loop
 => cpu_startup_entry
 => start_secondary
 ```

* htb qdisc

```
 12)               |  dev_queue_xmit() {
 12)   0.056 us    |    local_bh_disable();
 12)   0.101 us    |    netdev_pick_tx();
 12)   0.037 us    |    _raw_spin_lock();
 12)               |    htb_enqueue [sch_htb]() {
 12)   0.360 us    |      htb_classify [sch_htb]();
 12)   1.045 us    |    }
 12)               |    __qdisc_run() {
 12)   0.040 us    |      htb_dequeue [sch_htb]();
 12)               |      sch_direct_xmit() {
 12)               |        dev_hard_start_xmit() {
 12)   0.116 us    |          netif_skb_dev_features();
 12)   1.051 us    |          dev_queue_xmit_nit();
 12)   2.659 us    |          br_dev_xmit();
 12)   4.958 us    |        }
 12)   0.035 us    |        _raw_spin_lock();
 12)   5.623 us    |      }
 12)   6.373 us    |    }
 12)   0.037 us    |    local_bh_enable();
 12) + 10.378 us   |  }
 ```
