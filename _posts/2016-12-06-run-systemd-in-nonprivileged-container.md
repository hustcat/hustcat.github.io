---
layout: post
title: The right way to run systemd in a non-privileged container
date: 2016-12-06 12:00:30
categories: Container
tags: systemd
excerpt: The right way to run systemd in a non-privileged container
---

## Introduction

容器是否需要运行专门的init进程，在社区是一个颇有争议的话题。Docker推崇的哲学是一个进程一个容器(One process in one container)，所以不需要init。但这是一种理想化的想法，而在实际使用中，我们经常把容器当做`light VM`来使用，或者在容器运行多个进程。这时，在容器中运行一个init进程是非常有必要的[1](http://phusion.github.io/baseimage-docker/),[2](https://blog.phusion.nl/2015/01/20/docker-and-the-pid-1-zombie-reaping-problem/)。

社区很早就有相关的实现，比如[phusion/baseimage-docker](https://github.com/phusion/baseimage-docker)在基础镜中运行了一个轻量的init进程。实际上，如果把容器当做轻量级的虚拟机来用的，使用发行版自带的init程序，比如Redhat/CentOS上的Systemd、Ubuntu上面的Upstart，才是最好的选择。

然而，要在Docker容器中，正确的把Systemd或者Upstart运行起来，却不是一件容易的事情，有许多细节需要处理。Redhat的[Dan Walsh](https://rhatdan.wordpress.com)花了许多精力在这上面，参考[Running systemd within a Docker Container](https://developers.redhat.com/blog/2014/05/05/running-systemd-within-docker-container/)、[Running systemd in a non-privileged container](http://developers.redhat.com/blog/2016/09/13/running-systemd-in-a-non-privileged-container/)。

总的来说，使用`--privileged`来支持systemd，不是一个好的方法，这会将host的所有信息都暴露给容器。所以，最理想的实现就是在non-privileged容器中运行systemd。

## Systemd's requirement

要想在容器中把Systemd正确的运行起来，需要注意以下一些细节：

(1) Systemd expects `/run` is mounted as a tmpfs. 如果容器有sys_admin权限,可以不用指定`--tmpfs /run`,systemd在启动时自己会挂载;

(2) Systemd expects /sys/fs/cgroup filesystem is mounted. It can work with it being mounted read/only.
在CentOS6下，`/sys/fs/cgroup`默认是没有挂tmpfs.

(3) Systemd expects /sys/fs/cgroup/systemd be mounted read/write.如果容器有sys_admin权限，systemd会自行挂载cgroup filesystem到/sys/fs/cgroup/systemd.

(4) Systemd does not exit on `SIGTERM`.  Systemd defines that shutdown signal as `SIGRTMIN+3`, docker upstream should send this signal when user does a docker stop

`SIGTERM`会导致systemd进程重启，而`SIGRTMIN+3`才会使systemd shutdown，systemd对信号的处理逻辑如下：

```c
///core/manager.c
static int manager_dispatch_signal_fd(sd_event_source *source, int fd, uint32_t revents, void *userdata) {
///...
        for (;;) {
                n = read(m->signal_fd, &sfsi, sizeof(sfsi)); ///received signal
                if (n != sizeof(sfsi)) {

                        if (n >= 0)
                                return -EIO;

                        if (errno == EINTR || errno == EAGAIN)
                                break;

                        return -errno;
                }

                log_received_signal(sfsi.ssi_signo == SIGCHLD ||
                                    (sfsi.ssi_signo == SIGTERM && m->running_as == SYSTEMD_USER)
                                    ? LOG_DEBUG : LOG_INFO,
                                    &sfsi); ///write to /dev/console and journald

                switch (sfsi.ssi_signo) {

                case SIGCHLD:
                        sigchld = true;
                        break;

                case SIGTERM:
                        if (m->running_as == SYSTEMD_SYSTEM) {
                                /* This is for compatibility with the
                                 * original sysvinit */
                                m->exit_code = MANAGER_REEXECUTE; /// reexecute systemd
                                break;
                        }

                        /* Fall through */

                default: {

                        /* Starting SIGRTMIN+0 */
                        static const char * const target_table[] = {
                                [0] = SPECIAL_DEFAULT_TARGET,
                                [1] = SPECIAL_RESCUE_TARGET,
                                [2] = SPECIAL_EMERGENCY_TARGET,
                                [3] = SPECIAL_HALT_TARGET, ///"halt.target", SIGRTMIN+3
                                [4] = SPECIAL_POWEROFF_TARGET,
                                [5] = SPECIAL_REBOOT_TARGET,
                                [6] = SPECIAL_KEXEC_TARGET
                        };

                        /* Starting SIGRTMIN+13, so that target halt and system halt are 10 apart */
                        static const ManagerExitCode code_table[] = {
                                [0] = MANAGER_HALT,
                                [1] = MANAGER_POWEROFF,
                                [2] = MANAGER_REBOOT,
                                [3] = MANAGER_KEXEC
                        };

                        if ((int) sfsi.ssi_signo >= SIGRTMIN+0 &&
                            (int) sfsi.ssi_signo < SIGRTMIN+(int) ELEMENTSOF(target_table)) {
                                int idx = (int) sfsi.ssi_signo - SIGRTMIN; ///SIGRTMIN+3
                                manager_start_target(m, target_table[idx],
                                                     (idx == 1 || idx == 2) ? JOB_ISOLATE : JOB_REPLACE);
                                break;
                        }
```

当systemd收到`SIGRTMIN+3`时，会调用`halt.target`，转入halt流程。

(5) Systemd wants to have a unique /etc/machine-id to identify the system.

更多参考[Dan Walsh的文章](http://developers.redhat.com/blog/2016/09/13/running-systemd-in-a-non-privileged-container/).


## Build systemd+sshd image

* Dockerfile

```
FROM fedora:rawhide
MAINTAINER dbyin

ENV container docker

RUN mkdir /build
ADD . /build
RUN /build/build.sh && \
    /build/cleanup.sh

STOPSIGNAL 37
CMD ["/usr/sbin/init"]
```

参考[centos/systemd](https://hub.docker.com/r/centos/systemd/).

* build.sh

```sh
# cat build.sh 
#!/bin/bash
set -e
set -x

dnf -y update && dnf clean all

dnf -y install systemd procps util-linux-ng iproute net-tools && \
(cd /lib/systemd/system/sysinit.target.wants/; for i in *; do [ $i == systemd-tmpfiles-setup.service ] || rm -f $i; done); \
rm -f /lib/systemd/system/multi-user.target.wants/*;\
rm -f /etc/systemd/system/*.wants/*;\
rm -f /lib/systemd/system/local-fs.target.wants/*; \
rm -f /lib/systemd/system/sockets.target.wants/*udev*; \
rm -f /lib/systemd/system/sockets.target.wants/*initctl*; \
rm -f /lib/systemd/system/basic.target.wants/*;\
rm -f /lib/systemd/system/anaconda.target.wants/*;

# for seccomp: required by systemd231+
sed -ri 's/MemoryDenyWriteExecute/#MemoryDenyWriteExecute/g' /usr/lib/systemd/system/systemd-journald.service;
sed -ri 's/SystemCallFilter/#SystemCallFilter/g' /usr/lib/systemd/system/systemd-journald.service;

dnf -y install openssh openssh-server openssh-clients && dnf clean all


ssh-keygen -t rsa -f /etc/ssh/ssh_host_rsa_key
ssh-keygen -t dsa -f /etc/ssh/ssh_host_dsa_key
sed -ri 's/UsePAM yes/#UsePAM yes/g' /etc/ssh/sshd_config
sed -ri 's/#UsePAM no/UsePAM no/g' /etc/ssh/sshd_config
echo "root:root" | chpasswd
```

```sh
# cat cleanup.sh 
#!/bin/bash
set -e
set -x 
rm -rf /build
```

## Run container

在CentOS6上，需要手动将tmpfs挂载到`/sys/fs/cgroup`:

```sh
# mount -t tmpfs -o rw tmpfs /sys/fs/cgroup
# mkdir /sys/fs/cgroup/systemd
# docker run --cap-add=ALL  -it -v /sys/fs/cgroup:/sys/fs/cgroup:ro  --name=vm1 dbyin/systemd:fedora
Failed to insert module 'autofs4': No such file or directory
systemd 231 running in system mode. (+PAM +AUDIT +SELINUX +IMA -APPARMOR +SMACK +SYSVINIT +UTMP +LIBCRYPTSETUP +GCRYPT +GNUTLS +ACL +XZ +LZ4 +SECCOMP +BLKID +ELFUTILS +KMOD +IDN)
Detected virtualization docker.
Detected architecture x86-64.

Welcome to Fedora 26 (Rawhide)!

Set hostname to <badb3384e82b>.
Failed to open /dev/tty0: Operation not permitted
[  OK  ] Reached target Paths.
[  OK  ] Listening on Process Core Dump Socket.
[  OK  ] Listening on Journal Socket (/dev/log).
[  OK  ] Listening on Journal Socket.
[  OK  ] Reached target Swap.
[  OK  ] Created slice System Slice.
tmp.mount: Directory /tmp to mount over is not empty, mounting anyway.
         Mounting Temporary Directory...
[  OK  ] Reached target Slices.
[  OK  ] Created slice system-sshd\x2dkeygen.slice.
         Starting Journal Service...
[  OK  ] Mounted Temporary Directory.
[  OK  ] Reached target Local File Systems.
         Starting Create Volatile Files and Directories...
[  OK  ] Started Create Volatile Files and Directories.
[  OK  ] Started Journal Service.
```

可以看到，systemd正常运行，可以查看一下systemd的状态：

```sh
# docker exec -it vm1 /bin/sh
sh-4.3# ps -ef
UID        PID  PPID  C STIME TTY          TIME CMD
root         1     0  0 03:12 ?        00:00:00 /usr/sbin/init
root        14     1  0 03:12 ?        00:00:00 /usr/lib/systemd/systemd-journald
root        29     1  0 03:12 ?        00:00:00 /usr/sbin/sshd
root        85     0  0 03:35 ?        00:00:00 /bin/sh
root        91    85  0 03:36 ?        00:00:00 ps -ef
sh-4.3# systemctl status
● badb3384e82b
    State: running
     Jobs: 0 queued
   Failed: 0 units
    Since: Tue 2016-12-06 03:12:11 UTC; 29min ago
   CGroup: /
           ├─106 /bin/sh
           ├─114 systemctl status
           ├─115 more
           ├─system.slice
           │ ├─sshd.service
           │ │ └─29 /usr/sbin/sshd
           │ └─systemd-journald.service
           │   └─14 /usr/lib/systemd/systemd-journald
           └─init.scope
             └─1 /usr/sbin/init

sh-4.3# ls /sys/fs/cgroup/*/*
/sys/fs/cgroup/systemd/cgroup.clone_children  /sys/fs/cgroup/systemd/cgroup.sane_behavior  /sys/fs/cgroup/systemd/tasks
/sys/fs/cgroup/systemd/cgroup.event_control   /sys/fs/cgroup/systemd/notify_on_release
/sys/fs/cgroup/systemd/cgroup.procs           /sys/fs/cgroup/systemd/release_agent

/sys/fs/cgroup/systemd/init.scope:
cgroup.clone_children  cgroup.event_control  cgroup.procs  notify_on_release  tasks

/sys/fs/cgroup/systemd/system.slice:
 -.mount                 etc-resolv.conf.mount   proc-latency_stats.mount       proc-vmstat.mount
 cgroup.clone_children   notify_on_release       proc-loadavg.mount             sshd.service
 cgroup.event_control    proc-bus.mount          proc-meminfo.mount            'system-sshd\x2dkeygen.slice'
 cgroup.procs            proc-cpuinfo.mount      proc-sched_debug.mount         systemd-journald.service
 dev-mqueue.mount        proc-fs.mount           proc-stat.mount                systemd-tmpfiles-setup.service
 etc-hostname.mount      proc-irq.mount         'proc-sysrq\x2dtrigger.mount'   tasks
 etc-hosts.mount         proc-kcore.mount        proc-timer_list.mount          tmp.mount

/sys/fs/cgroup/systemd/user.slice:
cgroup.clone_children  cgroup.event_control  cgroup.procs  notify_on_release  tasks


sh-4.3# cat /proc/mounts
tmpfs /sys/fs/cgroup tmpfs ro,nosuid,nodev,noexec 0 0
cgroup /sys/fs/cgroup/systemd cgroup rw,nosuid,nodev,noexec,relatime,xattr,release_agent=/usr/lib/systemd/systemd-cgroups-agent,name=systemd 0 0
```

## reboot

当我们在容器内部执行`reboot`或者`systemctl reboot`命令时，systemd最终最执行`/usr/lib/systemd/systemd-shutdown`，后者调用系统调用`reboot`:

```c
///systemd/shutdown.c
int main(int argc, char *argv[]) {
///...

        case RB_POWER_OFF:
                log_info("Powering off.");
                break;

        case RB_HALT_SYSTEM:
                log_info("Halting system.");
                break;

        default:
                assert_not_reached("Unknown magic");
        }

        reboot(cmd); ///调用reboot系统调用
        if (errno == EPERM && in_container) { ///没有 CAP_SYS_BOOT
                /* If we are in a container, and we lacked
                 * CAP_SYS_BOOT just exit, this will kill our
                 * container for good. */
                log_info("Exiting container.");
                exit(0);
        }

}
```

* reboot系统调用

内核在reboot时候，会检查是否有CAP_SYS_BOOT权限；另外，如果是child pid namespace，并不会halt整个host:

```c
///kernel/sys.c
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	char buffer[256];
	int ret = 0;

	/* We only trust the superuser with rebooting the system. */
	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 &&
	                magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B &&
	                magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	/*
	 * If pid namespaces are enabled and the current task is in a child
	 * pid_namespace, the command is handled by reboot_pid_ns() which will
	 * call do_exit().
	 */
	ret = reboot_pid_ns(pid_ns, cmd); ///如果在子pid namespace
	if (ret)
		return ret;
///....
}


int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd)
{
	if (pid_ns == &init_pid_ns)
		return 0;

	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART2:
	case LINUX_REBOOT_CMD_RESTART:
		pid_ns->reboot = SIGHUP;
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
	case LINUX_REBOOT_CMD_HALT:
		pid_ns->reboot = SIGINT;
		break;
	default:
		return -EINVAL;
	}

	read_lock(&tasklist_lock);
	force_sig(SIGKILL, pid_ns->child_reaper); ///send SIGKILL to pid namespace init
	read_unlock(&tasklist_lock);

	do_exit(0);

	/* Not reached */
	return 0;
}
```

参考[pidns: add reboot_pid_ns() to handle the reboot syscall](https://github.com/torvalds/linux/commit/cf3f89214ef6a33fad60856bc5ffd7bb2fc4709b).

## /dev/console

systemd在启动时，会将启动日志输出到`/dev/console`:

```c
int unit_start(Unit *u) {
///...
        r = UNIT_VTABLE(u)->start(u); ///run unit
        if (r <= 0)
                return r;

        /* Log if the start function actually did something */
        unit_status_log_starting_stopping_reloading(u, JOB_START); ///write to journald
        unit_status_print_starting_stopping(u, JOB_START); /// write to /dev/console
        return r;
}
```

当我们以`-t`运行容器时，Docker会给容器创建[伪终端设备](http://man7.org/linux/man-pages/man4/pts.4.html)并将`Docker client`与[pseudoterminal master关联](http://hustcat.github.io/terminal-and-docker/)，这样，容器输出到控制台的日志最终都会传输到Client。

```sh
# docker run -it  --rm dbyin/busybox:latest /bin/sh
/ # ls -lh /dev/*
crw-------    1 root     root      136,   4 Dec  9 02:30 /dev/console
lrwxrwxrwx    1 root     root          11 Dec  8 08:53 /dev/core -> /proc/kcore
lrwxrwxrwx    1 root     root          13 Dec  8 08:53 /dev/fd -> /proc/self/fd
crw-rw-rw-    1 root     root        1,   7 Dec  8 08:53 /dev/full
crw-rw-rw-    1 root     root       10, 229 Dec  8 08:53 /dev/fuse
crw-rw-rw-    1 root     root        1,   3 Dec  8 08:53 /dev/null
lrwxrwxrwx    1 root     root           8 Dec  8 08:53 /dev/ptmx -> pts/ptmx
crw-rw-rw-    1 root     root        1,   8 Dec  8 08:53 /dev/random
lrwxrwxrwx    1 root     root          15 Dec  8 08:53 /dev/stderr -> /proc/self/fd/2
lrwxrwxrwx    1 root     root          15 Dec  8 08:53 /dev/stdin -> /proc/self/fd/0
lrwxrwxrwx    1 root     root          15 Dec  8 08:53 /dev/stdout -> /proc/self/fd/1
crw-rw-rw-    1 root     root        5,   0 Dec  8 08:53 /dev/tty
crw-rw-rw-    1 root     root        4,   0 Dec  8 08:53 /dev/tty0
crw-rw-rw-    1 root     root        4,   1 Dec  8 08:53 /dev/tty1
crw-rw-rw-    1 root     root        4,   2 Dec  8 08:53 /dev/tty2
crw-rw-rw-    1 root     root        4,   3 Dec  8 08:53 /dev/tty3
crw-rw-rw-    1 root     root        4,   4 Dec  8 08:53 /dev/tty4
crw-rw-rw-    1 root     root        1,   9 Dec  8 08:53 /dev/urandom
crw-rw-rw-    1 root     root        1,   5 Dec  8 08:53 /dev/zero

/dev/mqueue:
total 0

/dev/pts:
total 0
crw-rw-rw-    1 root     root        5,   2 Dec  8 08:53 ptmx

/dev/shm:
total 0
/ # echo hello > /dev/console 
hello
/ # tty
/dev/console
/ # echo hello > /dev/tty0
/bin/sh: can't create /dev/tty0: Operation not permitted
```

可以看到，当前容器的控制终端即为`/dev/console`。实际上，容器的`stdin/stdout/stderr`都指向host的伪终端设备`/dev/pts/4`:

```sh
[root@host]# ls /proc/40931/fd/* -lh
lrwx------ 1 root root 64 Dec  8 16:53 /proc/40931/fd/0 -> /4
lrwx------ 1 root root 64 Dec  8 16:53 /proc/40931/fd/1 -> /4
lrwx------ 1 root root 64 Dec  8 16:54 /proc/40931/fd/10 -> /dev/tty
lrwx------ 1 root root 64 Dec  8 16:53 /proc/40931/fd/2 -> /4

[root@host]# ls -lh /dev/pts/4
crw------- 1 root root 136, 4 Dec  9 10:47 /dev/pts/4
```

可以看到，容器里面的`/dev/console`的设备号与host的`/dev/pts/4`的设备号是一样的`136,4`。如果在host上，给`/dev/pts/4`写数据，会显示到容器的标准输出：

```sh
[root@host] # echo hello > /dev/pts/4

/ # hello  ## in container
```

实际上，libcontainer会用给容器创建的伪终端设备`/dev/pts/4`，会bind mount到`/dev/console`，所以，容器内部所有写`/dev/console`的日志都会传到Client。

```go
// InitializeMountNamespace sets up the devices, mount points, and filesystems for use inside a
// new mount namespace.
func InitializeMountNamespace(rootfs, console string, sysReadonly bool, mountConfig *MountConfig) error {
        ///创建设备节点
	if err := nodes.CreateDeviceNodes(rootfs, mountConfig.DeviceNodes); err != nil {
		return fmt.Errorf("create device nodes %s", err)
	}
        ///dev/console
	if err := SetupPtmx(rootfs, console, mountConfig.MountLabel); err != nil {
		return err
	}

}


// Setup initializes the proper /dev/console inside the rootfs path
func Setup(rootfs, consolePath, mountLabel string) error {
	oldMask := syscall.Umask(0000)
	defer syscall.Umask(oldMask)

	if err := os.Chmod(consolePath, 0600); err != nil {
		return err
	}

	if err := os.Chown(consolePath, 0, 0); err != nil {
		return err
	}

	if err := label.SetFileLabel(consolePath, mountLabel); err != nil {
		return fmt.Errorf("set file label %s %s", consolePath, err)
	}

	dest := filepath.Join(rootfs, "dev/console")

	f, err := os.Create(dest)
	if err != nil && !os.IsExist(err) {
		return fmt.Errorf("create %s %s", dest, err)
	}

	if f != nil {
		f.Close()
	}

	if err := syscall.Mount(consolePath, dest, "bind", syscall.MS_BIND, ""); err != nil {
		return fmt.Errorf("bind %s to %s %s", consolePath, dest, err)
	}

	return nil
}
```

```sh
### In container
/ # cat /proc/mounts 
devpts /dev/console devpts rw,relatime,gid=5,mode=620,ptmxmode=000 0 0
```

文章[Containers, pseudo TTYs, and backward compatibility](https://lwn.net/Articles/688809/)深入详细的讨论了伪终端与容器之间的一些问题。

## docker in docker

虽然systemd需要cgroup才能正常运行，但systemd只是使用cgroup来管理进程，并没有使用cgroup subsystem。但是，如果想在容器内部运行docker，则需要cgroup子系统正常挂载。当使用`--privileged`时，host的所有cgroup信息都会呈现在容器内部，这种做法不太安全。

内核从[4.6](http://hustcat.github.io/cgroup-namespace/)开始支持[cgroup namespace](http://man7.org/linux/man-pages/man7/cgroup_namespaces.7.html)，每个`cgroup namespace`有自己独立的cgroup视图；这样，就可以放心的在容器内部运行docker了。

在不支持`cgroup name`的内核上，我们仍然可以通过将host上容器的cgroup目录`bind mount`到容器内部，从而隐藏host上cgroup信息，但是，这种方式没有`cgroup namespace`安全。

## OCI hook

[OCI hook](https://github.com/opencontainers/runtime-spec/blob/master/config.md#hooks)可以让容器在运行用户指定的程序前后调用指定的命令，从而对容器做一些额外的操作。比如，可以在`prestart`准备好systemd运行所需要的环境(注：目前Docker暂时还不支持OCI hook)。

## Reference

一些讨论如果在容器运行systemd的文章：

* [Running systemd within a Docker Container](https://developers.redhat.com/blog/2014/05/05/running-systemd-within-docker-container/)
* [Running systemd in a non-privileged container](http://developers.redhat.com/blog/2016/09/13/running-systemd-in-a-non-privileged-container/)
* [Ubuntu 16.04 host: cannot run systemd inside unprivileged container](https://github.com/docker/docker/issues/28614)


systemd为了支持容器所做的一些改动：

* [Container Interface](https://www.freedesktop.org/wiki/Software/systemd/ContainerInterface/)
* [Container Integration](http://0pointer.net/blog/systemd-for-administrators-part-xxi.html)


一些介绍systemd的资料：

* [Rethinking PID 1](http://0pointer.de/blog/projects/systemd.html)
* [Systemd (简体中文)](https://wiki.archlinux.org/index.php/systemd_(%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87))
* [浅析 Linux 初始化 init 系统，第 3 部分: Systemd](https://www.ibm.com/developerworks/cn/linux/1407_liuming_init3/)
* [Systemd 入门教程：命令篇](http://www.ruanyifeng.com/blog/2016/03/systemd-tutorial-commands.html)