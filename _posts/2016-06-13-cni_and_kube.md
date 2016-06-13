---
layout: post
title: Container Network Interface
date: 2016-06-13 11:40:30
categories: Network
tags: CNI
excerpt: Container Network Interface
---

## CNI

### Bridge

* create network

```sh
# mkdir -p /etc/cni/net.d
# cat >/etc/cni/net.d/10-mynet.conf <<EOF
{
    "name": "mynet",
    "type": "bridge",
    "bridge": "cni0",
    "isGateway": true,
    "ipMasq": true,
    "ipam": {
        "type": "host-local",
        "subnet": "10.22.0.0/16",
        "routes": [
            { "dst": "0.0.0.0/0" }
        ]
    }
}
EOF
# cat >/etc/cni/net.d/99-loopback.conf <<EOF
{
    "type": "loopback"
}
EOF

# CNI_PATH=`pwd`/bin
# cd scripts
# CNI_PATH=$CNI_PATH ./priv-net-run.sh ifconfig        
contid=456a387326754f9
netnspath=/var/run/netns/456a387326754f9
eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 10.22.0.5  netmask 255.255.0.0  broadcast 0.0.0.0
        inet6 fe80::4ce9:51ff:fee1:2f97  prefixlen 64  scopeid 0x20<link>
        ether 4e:e9:51:e1:2f:97  txqueuelen 0  (Ethernet)
        RX packets 1  bytes 90 (90.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 1  bytes 90 (90.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0

lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536
        inet 127.0.0.1  netmask 255.0.0.0
        inet6 ::1  prefixlen 128  scopeid 0x10<host>
        loop  txqueuelen 0  (Local Loopback)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 0  bytes 0 (0.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```

`CNI_PATH=$CNI_PATH ./priv-net-run.sh` is equivalent to executing the following command: 

```sh
# export CNI_COMMAND=ADD
# export CNI_NETNS=/var/run/netns/456a387326754f9
# export CNI_CONTAINERID=456a387326754f9
# export CNI_IFNAME=eth0
# $CNI_PATH/bridge </etc/cni/net.d/10-mynet.conf

```

* delete network

```sh
# CNI_PATH=$CNI_PATH ./exec-plugins.sh del 456a387326754f9 /var/run/netns/456a387326754f9
```

Just like to run:

```sh
# export CNI_COMMAND=DEL
# export CNI_NETNS=/var/run/netns/456a387326754f9
# export CNI_CONTAINERID=456a387326754f9
# export CNI_IFNAME=eth0

# $CNI_PATH/bridge </etc/cni/net.d/10-mynet.conf
```

### Flannel

after start flannel:

```sh
# cat /run/flannel/subnet.env 
FLANNEL_NETWORK=192.168.0.0/16
FLANNEL_SUBNET=192.168.22.129/26
FLANNEL_MTU=1450
FLANNEL_IPMASQ=false
```

* create network

```sh
# cat >/etc/cni/net.d/10-mynet.conf <<EOF
{
    "name": "mynet",
"type": "flannel"
}
EOF


# CNI_PATH=$CNI_PATH ./priv-net-run.sh ifconfig
contid=6e3b681a422f5174
netnspath=/var/run/netns/6e3b681a422f5174
eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1450
        inet 192.168.22.130  netmask 255.255.255.192  broadcast 0.0.0.0
        inet6 fe80::d810:64ff:fe2c:831d  prefixlen 64  scopeid 0x20<link>
        ether da:10:64:2c:83:1d  txqueuelen 0  (Ethernet)
        RX packets 2  bytes 180 (180.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 1  bytes 90 (90.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0

lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536
        inet 127.0.0.1  netmask 255.0.0.0
        inet6 ::1  prefixlen 128  scopeid 0x10<host>
        loop  txqueuelen 0  (Local Loopback)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 0  bytes 0 (0.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```

* delete network

```sh
# CNI_PATH=$CNI_PATH ./exec-plugins.sh del 6e3b681a422f5174 /var/run/netns/6e3b681a422f5174
```

### MACVLAN

* create network

```sh
# cat /etc/cni/net.d/10-mynet.conf                                           
{
    "name": "mynet",
    "type": "macvlan",
    "master": "eth0",
    "ipam": {
        "type": "host-local",
        "subnet": "172.17.0.0/16",
        "routes": [
            { "dst": "0.0.0.0/0" }
        ]
    }
}

# CNI_PATH=$CNI_PATH CNI_ARGS="IP=172.17.20.20" ./priv-net-run.sh ifconfig  
contid=1d7a448c5de37308
netnspath=/var/run/netns/1d7a448c5de37308
eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 172.17.20.20  netmask 255.255.0.0  broadcast 0.0.0.0
        inet6 fe80::8485:5aff:fe3f:2339  prefixlen 64  scopeid 0x20<link>
        ether 86:85:5a:3f:23:39  txqueuelen 0  (Ethernet)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 1  bytes 90 (90.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0

lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536
        inet 127.0.0.1  netmask 255.0.0.0
        inet6 ::1  prefixlen 128  scopeid 0x10<host>
        loop  txqueuelen 0  (Local Loopback)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 0  bytes 0 (0.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```

* delete network

```sh
# CNI_PATH=$CNI_PATH ./exec-plugins.sh del 1d7a448c5de37308 /var/run/netns/1d7a448c5de37308
```

### CNI API

* Interface

```go
type RuntimeConf struct {
	ContainerID string
	NetNS       string
	IfName      string
	Args        [][2]string
}

type NetworkConfig struct {
	Network *types.NetConf
	Bytes   []byte
}

type CNI interface {
	AddNetwork(net *NetworkConfig, rt *RuntimeConf) (*types.Result, error)
	DelNetwork(net *NetworkConfig, rt *RuntimeConf) error
}
```

* interface implementation

```go
///implement CNI
type CNIConfig struct {
	Path []string
}

func (c *CNIConfig) AddNetwork(net *NetworkConfig, rt *RuntimeConf) (*types.Result, error) {
	pluginPath, err := invoke.FindInPath(net.Network.Type, c.Path)
	if err != nil {
		return nil, err
	}

	return invoke.ExecPluginWithResult(pluginPath, net.Bytes, c.args("ADD", rt))
}

func (c *CNIConfig) DelNetwork(net *NetworkConfig, rt *RuntimeConf) error {
	pluginPath, err := invoke.FindInPath(net.Network.Type, c.Path)
	if err != nil {
		return err
	}

	return invoke.ExecPluginWithoutResult(pluginPath, net.Bytes, c.args("DEL", rt))
}
```

## Kubernetes

### Implementation

```go
///pkg/kubelet/kubelet.go
func NewMainKubelet(
...
	if plug, err := network.InitNetworkPlugin(networkPlugins, networkPluginName, &networkHost{klet}, klet.hairpinMode, klet.nonMasqueradeCIDR); err != nil {
		return nil, err
	} else {
		klet.networkPlugin = plug
	}
```

* NetworkPlugin

```go
// Plugin is an interface to network plugins for the kubelet
type NetworkPlugin interface {
	// Init initializes the plugin.  This will be called exactly once
	// before any other methods are called.
	Init(host Host, hairpinMode componentconfig.HairpinMode, nonMasqueradeCIDR string) error

	// Called on various events like:
	// NET_PLUGIN_EVENT_POD_CIDR_CHANGE
	Event(name string, details map[string]interface{})

	// Name returns the plugin's name. This will be used when searching
	// for a plugin by name, e.g.
	Name() string

	// Returns a set of NET_PLUGIN_CAPABILITY_*
	Capabilities() utilsets.Int

	// SetUpPod is the method called after the infra container of
	// the pod has been created but before the other containers of the
	// pod are launched.
	SetUpPod(namespace string, name string, podInfraContainerID kubecontainer.ContainerID) error

	// TearDownPod is the method called before a pod's infra container will be deleted
	TearDownPod(namespace string, name string, podInfraContainerID kubecontainer.ContainerID) error

	// Status is the method called to obtain the ipv4 or ipv6 addresses of the container
	GetPodNetworkStatus(namespace string, name string, podInfraContainerID kubecontainer.ContainerID) (*PodNetworkStatus, error)

	// NetworkStatus returns error if the network plugin is in error state
	Status() error
}
```

* kubenet plugin

```go
func (plugin *kubenetNetworkPlugin) addContainerToNetwork(config *libcni.NetworkConfig, ifName, namespace, name string, id kubecontainer.ContainerID) (*cnitypes.Result, error) {
	rt, err := plugin.buildCNIRuntimeConf(ifName, id)
	if err != nil {
		return nil, fmt.Errorf("Error building CNI config: %v", err)
	}

	glog.V(3).Infof("Adding %s/%s to '%s' with CNI '%s' plugin and runtime: %+v", namespace, name, config.Network.Name, config.Network.Type, rt)
	res, err := plugin.cniConfig.AddNetwork(config, rt)
	if err != nil {
		return nil, fmt.Errorf("Error adding container to network: %v", err)
	}
	return res, nil
}
```

## CNI vs CNM

Refer to [Why Kubernetes doesn’t use libnetwork](http://blog.kubernetes.io/2016/01/why-Kubernetes-doesnt-use-libnetwork.html).

## Reference

* [CNI project](https://github.com/containernetworking/cni)
* [Container Networking Interface Proposal](https://github.com/containernetworking/cni/blob/master/SPEC.md)
* [CNI macvlan plugin](https://github.com/containernetworking/cni/blob/master/Documentation/macvlan.md)
* [Kube Network Plugins](http://kubernetes.io/docs/admin/network-plugins/)
* [Why Kubernetes doesn’t use libnetwork](http://blog.kubernetes.io/2016/01/why-Kubernetes-doesnt-use-libnetwork.html)
* [From network namespace to fabric overlays](http://events.linuxfoundation.org/sites/events/files/slides/Eugene%20Yakubovich_ContainerCon%20-%20netns-to-flannel.pdf)
* [Canal brings fine-grained policy to DC/OS and Apache Mesos via CNI](https://www.projectcalico.org/canal_brings_fine-grained_policy_to_dcos_and_apache_mesos_via_cni/)