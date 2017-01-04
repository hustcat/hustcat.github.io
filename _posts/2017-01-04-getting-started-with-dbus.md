---
layout: post
title: Getting started with D-BUS
date: 2017-01-04 19:00:30
categories: Linux
tags: dbus
excerpt: Getting started with D-BUS
---

## Introduction

[D-BUS](https://dbus.freedesktop.org/doc/dbus-specification.html)是一种进程间通信(IPC)机制，一般主要用于基于AF_UNIX套接字的本地进程间通信(local IPC)(当然也可以基于TCP/IP)实现跨主机的通信。


Linux上已经存在很多local IPC机制，比如FIFO、UNIX套接字等，为什么还要搞一个D-BUS呢？实际上，D-BUS采用了RPC(Remote Procedure Calling)的思想，它相当于本机的RPC，RPC相对于原始的FIFO、unix socket，是更加现代的通信方式，RPC框架本身会负责消息的编解码、安全验证等。这些会大大简化应用程序的开发。

D-Bus包含下面一些内容：

(1) `libdbus`: a low-level library

(2) `dbus-daemon`: a daemon based on `libdbus`. Handles and controls data transfers
between DBus peers

(3) two types of busses: a `system` and a `session` one. Each bus instance is managed
by a `dbus-daemon`

(4) a security mechanism using `policy` files


值得一提的是[systemd](http://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html)没有使用`libdbus`，而是使用自己实现的library。

## D-Bus Concepts

D-BUS协议是一个端到端（peer-to-peer or client-server）的通信协议，它包含一些基本的[概念](https://pythonhosted.org/txdbus/dbus_overview.html)：

![](https://dbus.freedesktop.org/doc/diagram.svg)

* 总线(bus)

相当于D-BUS的通信链路，应用之间通过总线进行通信。应用在总线上寻找`service`。有两种总线：

> A "system bus" for notifications from the system to user sessions, and to allow the system to request input from user sessions.
>
> A "session bus" used to implement desktop environments such as GNOME and KDE.

* 服务(service)

服务是提供IPC API的程序，每个服务都有一个`reverse domain name`结构的标识名称。比如`org.freedesktop.NetworkManager`对应系统总线上的`NetworkManager`；`org.freedesktop.login1`对应系统总线上的`systemd-logind`。

* 对象(object)

相当于通信的地址，每个`service`的`object`都通过`object path`来标识，`object path`类似文件系统的路径。比如`/org/freedesktop/login1`是服务`org.freedesktop.login1`的`manager`对象的路径。

* 接口(interfaces)

每个`object`包含一个或者多个`interfaces`。D-Bus interfaces define the methods and signals supported by D-Bus objects。

* 方法(method)

D-Bus methods may accept any number of arguments and may return any number of values, including none.

* signal

D-Bus signals provide a 1-to-many, publish-subscribe mechanism. Similar to method return values, D-Bus signals may contain an arbitrary ammount of data. Unlike methods however, signals are entirely asynchronous and may be emitted by D-Bus objects at any time.

* signature

`signature`用于描述参数的数据类型，比如`s`代表UTF-8字符串。

更多概念参考[DBus Overview](https://pythonhosted.org/txdbus/dbus_overview.html)。

## From shell

有一些D-BUS相关的工具，比如[busctl](http://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html)、[gdbus](https://www.freedesktop.org/software/gstreamer-sdk/data/docs/2012.5/gio/gdbus.html)、[dbus-send](https://dbus.freedesktop.org/doc/dbus-send.1.html)等，通过这些工具可以进行一些D-BUS相关的操作。

列出所有连接系统总线的端点：

```sh
# busctl
NAME                                   PID PROCESS         USER             CONNECTION    UNIT                      SESSION    DESCR
:1.0                                     1 systemd         root             :1.0          init.scope                -          -    
:1.1                                   516 polkitd         polkitd          :1.1          polkit.service            -          -    
:1.10                                 1305 busctl          root             :1.10         sshd.service              -          -    
:1.2                                   719 tuned           root             :1.2          tuned.service             -          -    
com.redhat.tuned                       719 tuned           root             :1.2          tuned.service             -          -    
fi.epitest.hostap.WPASupplicant          - -               -                (activatable) -                         -         
fi.w1.wpa_supplicant1                    - -               -                (activatable) -                         -         
org.freedesktop.DBus                     - -               -                -             -                         -          -    
org.freedesktop.PolicyKit1             516 polkitd         polkitd          :1.1          polkit.service            -          -    
org.freedesktop.hostname1                - -               -                (activatable) -                         -         
org.freedesktop.import1                  - -               -                (activatable) -                         -         
org.freedesktop.locale1                  - -               -                (activatable) -                         -         
org.freedesktop.login1                   - -               -                (activatable) -                         -         
org.freedesktop.machine1                 - -               -                (activatable) -                         -         
org.freedesktop.network1                 - -               -                (activatable) -                         -         
org.freedesktop.resolve1                 - -               -                (activatable) -                         -         
org.freedesktop.systemd1                 1 systemd         root             :1.0          init.scope                -          -    
org.freedesktop.timedate1                - -               -                (activatable) -                         -         
```

## D-Bus API of systemd

[systemd](https://www.freedesktop.org/wiki/Software/systemd/)提供了一些[D-BUS API](https://www.freedesktop.org/wiki/Software/systemd/dbus/)，通过API，我们可以进行systemd的各种操作，比如启动、停止服务等。

查看所有的API:

```sh
# gdbus introspect --system --dest org.freedesktop.systemd1 --object-path /org/freedesktop/systemd1
node /org/freedesktop/systemd1 {
  interface org.freedesktop.DBus.Peer {
    methods:
      Ping();
      GetMachineId(out s machine_uuid);
    signals:
    properties:
  };
  interface org.freedesktop.DBus.Introspectable {
    methods:
      Introspect(out s data);
    signals:
    properties:
  };
  interface org.freedesktop.DBus.Properties {
    methods:
      Get(in  s interface,
          in  s property,
          out v value);
      GetAll(in  s interface,
             out a{sv} properties);
      Set(in  s interface,
          in  s property,
          in  v value);
    signals:
      PropertiesChanged(s interface,
                        a{sv} changed_properties,
                        as invalidated_properties);
    properties:
  };
  interface org.freedesktop.systemd1.Manager {
    methods:
      GetUnit(in  s arg_0,
              out o arg_1);
      GetUnitByPID(in  u arg_0,
                   out o arg_1);
      LoadUnit(in  s arg_0,
               out o arg_1);
      StartUnit(in  s arg_0,
                in  s arg_1,
                out o arg_2);
      StartUnitReplace(in  s arg_0,
                       in  s arg_1,
                       in  s arg_2,
                       out o arg_3);
      StopUnit(in  s arg_0,
               in  s arg_1,
               out o arg_2);
      ReloadUnit(in  s arg_0,
                 in  s arg_1,
                 out o arg_2);
      RestartUnit(in  s arg_0,
                  in  s arg_1,
                  out o arg_2);
...
```

接口`org.freedesktop.systemd1.Manager`包含了systemd提供的主要操作方法。

* StartUnit/StopUnit

```sh
# busctl --system call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager GetUnit s crond.service 
o "/org/freedesktop/systemd1/unit/crond_2eservice"


# busctl --system call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager StopUnit ss crond.service replace
o "/org/freedesktop/systemd1/job/904"

# systemctl status crond.service
● crond.service - Command Scheduler
   Loaded: loaded (/usr/lib/systemd/system/crond.service; enabled; vendor preset: enabled)
   Active: inactive (dead) since 四 2017-01-05 03:13:29 CST; 4s ago
  Process: 656 ExecStart=/usr/sbin/crond -n $CRONDARGS (code=exited, status=0/SUCCESS)
 Main PID: 656 (code=exited, status=0/SUCCESS)
...

# busctl --system call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager StartUnit ss crond.service replace
o "/org/freedesktop/systemd1/job/905"

# systemctl status crond.service
● crond.service - Command Scheduler
   Loaded: loaded (/usr/lib/systemd/system/crond.service; enabled; vendor preset: enabled)
   Active: active (running) since 四 2017-01-05 03:13:53 CST; 1s ago
 Main PID: 12277 (crond)
   Memory: 624.0K
   CGroup: /system.slice/crond.service
           └─12277 /usr/sbin/crond -n
...
```

## Reference

* [D-Bus Specification](https://dbus.freedesktop.org/doc/dbus-specification.html)
* [Get on the D-BUS](http://www.linuxjournal.com/article/7744)
* [The new sd-bus API of systemd](http://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html)
* [Understanding D-Bus](http://free-electrons.com/pub/conferences/2016/meetup/dbus/josserand-dbus-meetup.pdf)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
* [The D-Bus API of systemd/PID 1](https://www.freedesktop.org/wiki/Software/systemd/dbus/)
* [使用 D-BUS 连接桌面应用程序](http://www.ibm.com/developerworks/cn/linux/l-dbus.html)