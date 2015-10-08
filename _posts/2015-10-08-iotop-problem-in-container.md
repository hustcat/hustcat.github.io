---
layout: post
title: Can't run iotop in container
date: 2015-10-08 17:27:30
categories: Linux
tags: misc
excerpt: Run iotop failed in container
---

在容器内部执行iotop，会报下面的错误(ENOENT)：

```
Traceback (most recent call last):
  File "/usr/sbin/iotop", line 16, in <module>
    main()
  File "/usr/lib/python2.6/site-packages/iotop/ui.py", line 559, in main
    main_loop()
  File "/usr/lib/python2.6/site-packages/iotop/ui.py", line 549, in <lambda>
    main_loop = lambda: run_iotop(options)
  File "/usr/lib/python2.6/site-packages/iotop/ui.py", line 447, in run_iotop
    return curses.wrapper(run_iotop_window, options)
  File "/usr/lib64/python2.6/curses/wrapper.py", line 43, in wrapper
    return func(stdscr, *args, **kwds)
  File "/usr/lib/python2.6/site-packages/iotop/ui.py", line 437, in run_iotop_window
    taskstats_connection = TaskStatsNetlink(options)
  File "/usr/lib/python2.6/site-packages/iotop/data.py", line 113, in __init__
    self.family_id = controller.get_family_id('TASKSTATS')
  File "/usr/lib/python2.6/site-packages/iotop/genetlink.py", line 54, in get_family_id
    m = self.conn.recv()
  File "/usr/lib/python2.6/site-packages/iotop/netlink.py", line 190, in recv
    raise err
OSError: Netlink error: No such file or directory (2)
```

iotop是通过内核的[taskstats](https://www.kernel.org/doc/Documentation/accounting/taskstats.txt)接口获取统计信息的。taskstats是基于[generic netlink](http://www.linuxfoundation.org/collaborate/workgroups/networking/generic_netlink_howto)实现的，而且taskstats不支持net namespace。

```c
static struct genl_family family = {
	.id		= GENL_ID_GENERATE,
	.name		= TASKSTATS_GENL_NAME,
	.version	= TASKSTATS_GENL_VERSION,
	.maxattr	= TASKSTATS_CMD_ATTR_MAX,
};


static int ctrl_dumpfamily(struct sk_buff *skb, struct netlink_callback *cb)
{
 int i, n = 0;
 struct genl_family *rt;
 struct net *net = sock_net(skb->sk);
 int chains_to_skip = cb->args[0];
 int fams_to_skip = cb->args[1];
 for (i = chains_to_skip; i < GENL_FAM_TAB_SIZE; i++) {
  n = 0;
  list_for_each_entry(rt, genl_family_chain(i), family_list) {
   if (!rt->netnsok && !net_eq(net, &init_net))///don't support netnamespace
    continue;
```

写程序调用genl_ctrl_resolve，会报下面的错误：

```sh
#./taskstats  --pid 1                     
genl_ctrl_resolve: Generic Netlink Family not found (errno = No such file or directory)
```

参考[这里](https://github.com/tgraf/libnl-1.1-stable/blob/master/lib/genl/ctrl.c#L250)。

内核[函数调用](/assets/genl_ctrl_resolve.log)：

![](/assets/2015-10-08-iotop-problem-in-container.jpg)

OpenVZ的[Andrey Vagin](https://www.mail-archive.com/linux-kernel@vger.kernel.org/msg924686.html)已经提交了相关[patch](https://www.mail-archive.com/linux-kernel@vger.kernel.org/msg924689.html)，但还没有合到内核主线。

# 测试程序

```c
/*
 * gcc -o taskstats taskstats_nl.c -lnl
*/
#include <stdlib.h>
#include <linux/taskstats.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

void usage(int argc, char *argv[]);
int msg_recv_cb(struct nl_msg *, void *);

int main(int argc, char *argv[])
{
    struct nl_handle *sock;
    struct nl_msg *msg;
    int family;
    int pid = -1;
    char *cpumask;

    if (argc > 2 && strcmp(argv[1], "--pid") == 0) {
        pid = atoi(argv[2]);
    } else if (argc > 2 && strcmp(argv[1], "--cpumask") == 0) {
        cpumask = argv[2];
    } else {
        usage(argc, argv);
        exit(1);
    }

    sock = nl_handle_alloc();
    if(!sock){
		nl_perror("nl_handle_alloc");
		exit(1);
    }
    // Connect to generic netlink socket on kernel side
    if(genl_connect(sock) < 0){
		nl_perror("genl_connect");
  		exit(1);	
    }

    // get the id for the TASKSTATS generic family
    family = genl_ctrl_resolve(sock, "TASKSTATS");
    if(family < 0){
		nl_perror("genl_ctrl_resolve");
		exit(1);
    }

    // register for task exit events
    msg = nlmsg_alloc();

    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_REQUEST,
            TASKSTATS_CMD_GET, TASKSTATS_VERSION);
    if (pid > 0) {
        nla_put_u32(msg, TASKSTATS_CMD_ATTR_PID, pid);
    } else {
        nla_put_string(msg, TASKSTATS_CMD_ATTR_REGISTER_CPUMASK,
                   cpumask);
    }

    nl_send_auto_complete(sock, msg);
    nlmsg_free(msg);

    // specify a callback for inbound messages
    //nl_socket_modify_cb(sock, NL_CB_MSG_IN, NL_CB_CUSTOM, msg_recv_cb, NULL);
    if (pid > 0) {
        if(nl_recvmsgs_default(sock) < 0){
			nl_perror("nl_recvmsgs_default");
			exit(-1);
		}
    } else {
        while (1)
            nl_recvmsgs_default(sock);
    }
    return 0;
}

void usage(int argc, char *argv[])
{
    printf("USAGE: %s option\nOptions:\n"
           "\t--pid pid : get statistics during a task's lifetime.\n"
           "\t--cpumask mask : obtain statistics for tasks which are exiting. \n"
           "\t\tThe cpumask is specified as an ascii string of comma-separated \n"
           "\t\tcpu ranges e.g. to listen to exit data from cpus 1,2,3,5,7,8\n"
           "\t\tthe cpumask would be '1-3,5,7-8'.\n", argv[0]);
}

#define printllu(str, value)    printf("%s: %llu\n", str, value)

int msg_recv_cb(struct nl_msg *nlmsg, void *arg)
{
    struct nlmsghdr *nlhdr;
    struct nlattr *nlattrs[TASKSTATS_TYPE_MAX + 1];
    struct nlattr *nlattr;
    struct taskstats *stats;
    int rem;

    nlhdr = nlmsg_hdr(nlmsg);

    // validate message and parse attributes
    genlmsg_parse(nlhdr, 0, nlattrs, TASKSTATS_TYPE_MAX, 0);

    if (nlattr = nlattrs[TASKSTATS_TYPE_AGGR_PID]) {
        stats = nla_data(nla_next(nla_data(nlattr), &rem));

        printf("---\n");
        printf("pid: %u\n", stats->ac_pid);
        printf("command: %s\n", stats->ac_comm);
        printf("status: %u\n", stats->ac_exitcode);
        printf("time:\n");
        printf("  start: %u\n", stats->ac_btime);

        printllu("  elapsed", stats->ac_etime);
        printllu("  user", stats->ac_utime);
        printllu("  system", stats->ac_stime);
        printf("memory:\n");
        printf("  bytetime:\n");
        printllu("    rss", stats->coremem);
        printllu("    vsz", stats->virtmem);
        printf("  peak:\n");
        printllu("    rss", stats->hiwater_rss);
        printllu("    vsz", stats->hiwater_vm);
        printf("io:\n");
        printf("  bytes:\n");
        printllu("    read", stats->read_char);
        printllu("    write", stats->write_char);
        printf("  syscalls:\n");
        printllu("    read", stats->read_syscalls);
        printllu("    write", stats->write_syscalls);
    }
    return 0;
}
```

# 相关资料

* [libnl-1.1.x](https://github.com/tgraf/libnl-1.1-stable)
* [libnl](http://www.infradead.org/~tgr/libnl/)
* [libnl从内核获取taskstats信息](http://onestraw.net/linux/libnl-taskstats/)

