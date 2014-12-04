---
layout: post
title:  Docker解析：配置与权限管理
date: 2014-12-04 15:43:30
categories: Linux
tags: docker
excerpt: Docker中的配置与权限管理
---

Docker中的配置
------
Docker中的配置主要包括daemon进程和容器的配置。

daemon的配置
---
daemon/config.go

```go
type Config struct {
	Pidfile                     string
	Root                        string
	AutoRestart                 bool
	Dns                         []string
	DnsSearch                   []string
	Mirrors                     []string
	EnableIptables              bool
	EnableIpForward             bool
	EnableIpMasq                bool
	DefaultIp                   net.IP
	BridgeIface                 string
	BridgeIP                    string
	FixedCIDR                   string
	InsecureRegistries          []string
	InterContainerCommunication bool
	GraphDriver                 string
	GraphOptions                []string
	ExecDriver                  string
	Mtu                         int
	DisableNetwork              bool
	EnableSelinuxSupport        bool
	Context                     map[string][]string
	TrustKeyPath                string
	Labels                      []string
}
```

容器的配置
---
容器的配置分两部分，一部分与Host无关，另一部分与Host相关。

与Host无关的配置runconfig/config.go

```go
type Config struct {
	Hostname        string
	Domainname      string
	User            string
	Memory          int64  // Memory limit (in bytes)
	MemorySwap      int64  // Total memory usage (memory + swap); set `-1' to disable swap
	CpuShares       int64  // CPU shares (relative weight vs. other containers)
	Cpuset          string // Cpuset 0-2, 0,1
	AttachStdin     bool
	AttachStdout    bool
	AttachStderr    bool
	PortSpecs       []string // Deprecated - Can be in the format of 8080/tcp
	ExposedPorts    map[nat.Port]struct{}
	Tty             bool // Attach standard streams to a tty, including stdin if it is not closed.
	OpenStdin       bool // Open stdin
	StdinOnce       bool // If true, close stdin after the 1 attached client disconnects.
	Env             []string
	Cmd             []string
	Image           string // Name of the image as it was passed by the operator (eg. could be symbolic)
	Volumes         map[string]struct{}
	WorkingDir      string
	Entrypoint      []string
	NetworkDisabled bool
	MacAddress      string
	OnBuild         []string
}
```
与Host相关的配置runconfig/hostconfig.go

```go
type HostConfig struct {
	Binds           []string
	ContainerIDFile string
	LxcConf         []utils.KeyValuePair
	Privileged      bool
	PortBindings    nat.PortMap
	Links           []string
	PublishAllPorts bool
	Dns             []string
	DnsSearch       []string
	ExtraHosts      []string
	VolumesFrom     []string
	Devices         []DeviceMapping
	NetworkMode     NetworkMode
	IpcMode         IpcMode
	CapAdd          []string
	CapDrop         []string
	RestartPolicy   RestartPolicy
	SecurityOpt     []string
}
```

libcontainer.Config
---

容器在启动时，转成libcontainer.Config，传给libtainer。

```go
// Config defines configuration options for executing a process inside a contained environment.
type Config struct {
	// Mount specific options.
	MountConfig *MountConfig `json:"mount_config,omitempty"`

	// Pathname to container's root filesystem
	RootFs string `json:"root_fs,omitempty"`

	// Hostname optionally sets the container's hostname if provided
	Hostname string `json:"hostname,omitempty"`

	// User will set the uid and gid of the executing process running inside the container
	User string `json:"user,omitempty"`

	// WorkingDir will change the processes current working directory inside the container's rootfs
	WorkingDir string `json:"working_dir,omitempty"`

	// Env will populate the processes environment with the provided values
	// Any values from the parent processes will be cleared before the values
	// provided in Env are provided to the process
	Env []string `json:"environment,omitempty"`

	// Tty when true will allocate a pty slave on the host for access by the container's process
	// and ensure that it is mounted inside the container's rootfs
	Tty bool `json:"tty,omitempty"`

	// Namespaces specifies the container's namespaces that it should setup when cloning the init process
	// If a namespace is not provided that namespace is shared from the container's parent process
	Namespaces map[string]bool `json:"namespaces,omitempty"`

	// Capabilities specify the capabilities to keep when executing the process inside the container
	// All capbilities not specified will be dropped from the processes capability mask
	Capabilities []string `json:"capabilities,omitempty"`

	// Networks specifies the container's network setup to be created
	Networks []*Network `json:"networks,omitempty"`

	// Ipc specifies the container's ipc setup to be created
	IpcNsPath string `json:"ipc,omitempty"`

	// Routes can be specified to create entries in the route table as the container is started
	Routes []*Route `json:"routes,omitempty"`

	// Cgroups specifies specific cgroup settings for the various subsystems that the container is
	// placed into to limit the resources the container has available
	Cgroups *cgroups.Cgroup `json:"cgroups,omitempty"`

	// AppArmorProfile specifies the profile to apply to the process running in the container and is
	// change at the time the process is execed
	AppArmorProfile string `json:"apparmor_profile,omitempty"`

	// ProcessLabel specifies the label to apply to the process running in the container.  It is
	// commonly used by selinux
	ProcessLabel string `json:"process_label,omitempty"`

	// RestrictSys will remount /proc/sys, /sys, and mask over sysrq-trigger as well as /proc/irq and
	// /proc/bus
	RestrictSys bool `json:"restrict_sys,omitempty"`
}
```

docker中的权限
------

docker提供了下面几个参数，用于管理容器的权限：

> --cap-add: Add Linux capabilities
>
> --cap-drop: Drop Linux capabilities
>
> --privileged=false: Give extended privileges to this container
>
> --device=[]: Allows you to run devices inside the container without the --privileged flag.

默认情况下，docker的容器中的root的权限是有严格限制的，比如，网络管理（NET_ADMIN等很多权限都是没有的。

```sh
[root@yinye ~]# docker run -it --rm ubuntu:14.04 /bin/bash
root@fdf8fc8ecf4c:/# ip link set eth0 down
RTNETLINK answers: Operation not permitted
```
可以看到，默认情况下，在容器中进行网络管理相关操作会失败。

```sh
[root@yinye ~]# docker run --cap-add=NET_ADMIN -it --rm ubuntu:14.04 /bin/bash
root@0355d3b31934:/# ip link set eth0 down
```
加上NET_ADMIN便可顺利执行。

docker中的权限类型
---
libcontainer/security/capabilities

```go
var capabilityList = Capabilities{
	{Key: "SETPCAP", Value: capability.CAP_SETPCAP},
	{Key: "SYS_MODULE", Value: capability.CAP_SYS_MODULE},
	{Key: "SYS_RAWIO", Value: capability.CAP_SYS_RAWIO},
	{Key: "SYS_PACCT", Value: capability.CAP_SYS_PACCT},
	{Key: "SYS_ADMIN", Value: capability.CAP_SYS_ADMIN},
	{Key: "SYS_NICE", Value: capability.CAP_SYS_NICE},
	{Key: "SYS_RESOURCE", Value: capability.CAP_SYS_RESOURCE},
	{Key: "SYS_TIME", Value: capability.CAP_SYS_TIME},
	{Key: "SYS_TTY_CONFIG", Value: capability.CAP_SYS_TTY_CONFIG},
	{Key: "MKNOD", Value: capability.CAP_MKNOD},
	{Key: "AUDIT_WRITE", Value: capability.CAP_AUDIT_WRITE},
	{Key: "AUDIT_CONTROL", Value: capability.CAP_AUDIT_CONTROL},
	{Key: "MAC_OVERRIDE", Value: capability.CAP_MAC_OVERRIDE},
	{Key: "MAC_ADMIN", Value: capability.CAP_MAC_ADMIN},
	{Key: "NET_ADMIN", Value: capability.CAP_NET_ADMIN},
	{Key: "SYSLOG", Value: capability.CAP_SYSLOG},
	{Key: "CHOWN", Value: capability.CAP_CHOWN},
	{Key: "NET_RAW", Value: capability.CAP_NET_RAW},
	{Key: "DAC_OVERRIDE", Value: capability.CAP_DAC_OVERRIDE},
	{Key: "FOWNER", Value: capability.CAP_FOWNER},
	{Key: "DAC_READ_SEARCH", Value: capability.CAP_DAC_READ_SEARCH},
	{Key: "FSETID", Value: capability.CAP_FSETID},
	{Key: "KILL", Value: capability.CAP_KILL},
	{Key: "SETGID", Value: capability.CAP_SETGID},
	{Key: "SETUID", Value: capability.CAP_SETUID},
	{Key: "LINUX_IMMUTABLE", Value: capability.CAP_LINUX_IMMUTABLE},
	{Key: "NET_BIND_SERVICE", Value: capability.CAP_NET_BIND_SERVICE},
	{Key: "NET_BROADCAST", Value: capability.CAP_NET_BROADCAST},
	{Key: "IPC_LOCK", Value: capability.CAP_IPC_LOCK},
	{Key: "IPC_OWNER", Value: capability.CAP_IPC_OWNER},
	{Key: "SYS_CHROOT", Value: capability.CAP_SYS_CHROOT},
	{Key: "SYS_PTRACE", Value: capability.CAP_SYS_PTRACE},
	{Key: "SYS_BOOT", Value: capability.CAP_SYS_BOOT},
	{Key: "LEASE", Value: capability.CAP_LEASE},
	{Key: "SETFCAP", Value: capability.CAP_SETFCAP},
	{Key: "WAKE_ALARM", Value: capability.CAP_WAKE_ALARM},
	{Key: "BLOCK_SUSPEND", Value: capability.CAP_BLOCK_SUSPEND},
}
```

libcontainer的权限实际上也是借助Linux kernel capabilities实现的。这些权限的详细意义参考[这里](http://man7.org/linux/man-pages/man7/capabilities.7.html)

更多介绍参考
* [Taking Advantage of Linux Capabilities][ref1]
* [How Linux Capability Works in 2.6.25][ref2]
[ref1]: http://www.linuxjournal.com/article/5737
[ref2]: http://www.cis.syr.edu/~wedu/seed/Documentation/Linux/How_Linux_Capability_Works.pdf

容器的默认权限
---

```go
// New returns the docker default configuration for libcontainer
func New() *libcontainer.Config {
	container := &libcontainer.Config{
		Capabilities: []string{
			"CHOWN",
			"DAC_OVERRIDE",
			"FSETID",
			"FOWNER",
			"MKNOD",
			"NET_RAW",
			"SETGID",
			"SETUID",
			"SETFCAP",
			"SETPCAP",
			"NET_BIND_SERVICE",
			"SYS_CHROOT",
			"KILL",
			"AUDIT_WRITE",
		},
		Namespaces: map[string]bool{
			"NEWNS":  true,
			"NEWUTS": true,
			"NEWIPC": true,
			"NEWPID": true,
			"NEWNET": true,
		},
		Cgroups: &cgroups.Cgroup{
			Parent:          "docker",
			AllowAllDevices: false,
		},
		MountConfig: &libcontainer.MountConfig{},
	}

	if apparmor.IsEnabled() {
		container.AppArmorProfile = "docker-default"
	}

	return container
}
```
参考[这里](
https://github.com/docker/docker/blob/master/daemon/execdriver/native/template/default_template.go)


参考
------
https://github.com/docker/docker/blob/master/docs/sources/articles/security.md
https://docs.docker.com/reference/run/#runtime-privilege-linux-capabilities-and-lxc-configuration
