---
layout: post
title:  Docker解析：数据卷(Data Volume)的实现
date: 2014-12-12 18:43:30
categories: Linux
tags: docker
excerpt: Docker中数据卷的实现
---

Docker中的容器一旦删除，容器本身的文件系统dm thin volume就会被删除，容器中的所有数据也将随之删除。但有的时候，我们想要数据，比如日志、或者其它需要持久化的数据，不随容器的删除而删除。还有的时候，我们希望在同一台Host的容器之间可以共享数据。

为此，Docker提供了数据卷(data volume)，数据卷除了可以持久化数据，还可以用于容器之间共享数据。

有2个对数据卷相关的参数：

```sh
-v=[]: Create a bind mount with: [host-dir]:[container-dir]:[rw|ro].
       If "container-dir" is missing, then docker creates a new volume.
--volumes-from="": Mount all volumes from the given container(s)
```

先看参数-v，它有3个变量：

host-dir表示Host上的目录，如果不存在，docker会自动在Host上创建该目录；
container-dir表示容器内部对应的目录，如果不存在，docker也会在容器内部创建该目录；
rw|ro用于控制卷的读写权限。

一般来说，-v有两种方式，一是指定host-dir：

```sh
# docker run -v /data/vm3:/data -it --rm ubuntu:14.04 /bin/bash
```

二是不指定host-dir：

```sh
# docker run -v /volume1 -it --rm --name=test1 ubuntu:14.04 /bin/bash
```

前者实际上是对应bind mount。

```sh
# cat /var/lib/docker/containers/$ID/hostconfig.json |python -mjson.tool
{
    "Binds": [
        "/data/vm3:/data"
    ],
```

后者却不一样：

```sh
# cat /var/lib/docker/containers/$ID/config.json |python -mjson.tool
{
   "Config": {
…
        "Volumes": {
            "/data": {}
        },
```

可以看到如果指定了host-dir，配置信息保存在hostconfig；如果不指定，配置信息保存在config中。

```go
func Parse(cmd *flag.FlagSet, args []string) (*Config, *HostConfig, *flag.FlagSet, error) {
	cmd.Var(&flVolumes, []string{"v", "-volume"}, "Bind mount a volume (e.g., from the host: -v /host:/container, from Docker: -v /container)")
	cmd.Var(&flVolumesFrom, []string{"#volumes-from", "-volumes-from"}, "Mount volumes from the specified container(s)")

	var binds []string
	// add any bind targets to the list of container volumes
	for bind := range flVolumes.GetMap() {
		if arr := strings.Split(bind, ":"); len(arr) > 1 {
			if arr[1] == "/" {
				return nil, nil, cmd, fmt.Errorf("Invalid bind mount: destination can't be '/'")
			}
			// after creating the bind mount we want to delete it from the flVolumes values because
			// we do not want bind mounts being committed to image configs
			binds = append(binds, bind)
			flVolumes.Delete(bind)
       }

	config := &Config{
...
		Volumes:         flVolumes.GetMap(),


	hostConfig := &HostConfig{
		Binds:           binds,
...
		VolumesFrom:     flVolumesFrom.GetAll(),
```

实际上，对于后者这种没有指定Host目录的情况，Docker会在Host的/var/lib/docker/vfs/dir/目录生成一个随机的目录，然后挂载到容器的/volume1。

```sh
# docker inspect test1
[{
...
    "Volumes": {
        "/volume1": "/var/lib/docker/vfs/dir/144d7df40fc569b25221b4d2dd14b5b21ba840accbbac627958080bbb04db109"
    },
    "VolumesRW": {
        "/volume1": true
    }
}
]
[root@yinye ~]#ls /var/lib/docker/vfs/dir/144d7df40fc569b25221b4d2dd14b5b21ba840accbbac627958080bbb04db109
test.txt
[root@yinye ~]# cat /var/lib/docker/vfs/dir/144d7df40fc569b25221b4d2dd14b5b21ba840accbbac627958080bbb04db109/test.txt 
volume1
```

如果容器删除，对应的Host目录也会随之删除。

> 注意这里，Config中保存用户指定的信息，而Volumes保存运行时信息。

数据结构
------
Docker层面有2个与数据卷相关的数据结构：

Volume

```go
//volumes/volume.go，用于描述容器的一个数据卷
type Volume struct {
	ID          string  //随机生成
	Path        string //Host的目录
	IsBindMount bool  //是否是bind mount
	Writable    bool
	containers  map[string]struct{} //卷所属的容器
	configPath  string
	repository  *Repository
	lock        sync.Mutex
}
```

Mount

```go
//daemon/volumes.go, 描述卷在容器内部的挂载信息
type Mount struct {
	MountToPath string  //容器内部的目录
	container   *Container
	volume      *volumes.Volume //数据卷
	Writable    bool //是否可写
	copyData    bool
}
```

创建数据卷
------

docker解析mount信息
---

![](/assets/2014-12-12-docker-data-volume.jpg)

创建数据卷的主要逻辑是在函数parseVolumeMountConfig中实现的：

```go
func (container *Container) parseVolumeMountConfig() (map[string]*Mount, error) {
	var mounts = make(map[string]*Mount)
    //(1)先处理bind mount，即-v指定了Host目录
	// Get all the bind mounts
	for _, spec := range container.hostConfig.Binds {
       //path为Host的目录,mountToPath为容器中的目录
		path, mountToPath, writable, err := parseBindMountSpec(spec)
		if err != nil {
			return nil, err
		}
		// Check if a volume already exists for this and use it
		vol, err := container.daemon.volumes.FindOrCreateVolume(path, writable)
		if err != nil {
			return nil, err
		}
		mounts[mountToPath] = &Mount{
			container:   container,
			volume:      vol,
			MountToPath: mountToPath,
			Writable:    writable,
		}
	}

	// Get the rest of the volumes
	for path := range container.Config.Volumes {
		// Check if this is already added as a bind-mount
		path = filepath.Clean(path) //容器内部的目录
		if _, exists := mounts[path]; exists {
			continue
		}

		// Check if this has already been created
		if _, exists := container.Volumes[path]; exists {
			continue
		}
      //在Host创建一个随机目录
		vol, err := container.daemon.volumes.FindOrCreateVolume("", true)
		if err != nil {
			return nil, err
		}
		mounts[path] = &Mount{
			container:   container,
			MountToPath: path,
			volume:      vol,
			Writable:    true,
			copyData:    true,
		}
	}

	return mounts, nil
}
```

最后解析的结果放在Container.Volumes：

```go
type Container struct {
...
	// Maps container paths to volume paths.  The key in this is the path to which
	// the volume is being mounted inside the container.  Value is the path of the
	// volume on disk
	Volumes map[string]string
	// Store rw/ro in a separate structure to preserve reverse-compatibility on-disk.
	// Easier than migrating older container configs :)
	VolumesRW  map[string]bool
...
}
```

如下：

```go
func (m *Mount) initialize() error {
...
	m.container.VolumesRW[m.MountToPath] = m.Writable
	m.container.Volumes[m.MountToPath] = m.volume.Path
	m.volume.AddContainer(m.container.ID)
	if m.Writable && m.copyData {
		// Copy whatever is in the container at the mntToPath to the volume
		copyExistingContents(containerMntPath, m.volume.Path)
	}

	return nil
}
```

Docker的mount信息转换成libcontainer的mount信息
---

docker.Mount --> libcontainer.Mount

```go
func (d *driver) setupMounts(container *libcontainer.Config, c *execdriver.Command) error {
 //c.Mounts信息由Container.Volumes转换而来，参考(container *Container) setupMounts()
	for _, m := range c.Mounts {
		container.MountConfig.Mounts = append(container.MountConfig.Mounts, &mount.Mount{
			Type:        "bind",
			Source:      m.Source,
			Destination: m.Destination,
			Writable:    m.Writable,
			Private:     m.Private,
			Slave:       m.Slave,
		})
	}

	return nil
}
```

bind mount
---

最后的挂载由libcontainer完成：

```go
//libcontainer/mount/init.go
// InitializeMountNamespace sets up the devices, mount points, and filesystems for use inside a
// new mount namespace.
func InitializeMountNamespace(rootfs, console string, sysReadonly bool, mountConfig *MountConfig) error {
...
	// apply any user specified mounts within the new mount namespace
	for _, m := range mountConfig.Mounts {
		if err := m.Mount(rootfs, mountConfig.MountLabel); err != nil {
			return err
		}
	}
...
}

func (m *Mount) Mount(rootfs, mountLabel string) error {
	switch m.Type {
	case "bind":
		return m.bindMount(rootfs, mountLabel)
	case "tmpfs":
		return m.tmpfsMount(rootfs, mountLabel)
	default:
		return fmt.Errorf("unsupported mount type %s for %s", m.Type, m.Destination)
	}
}
```

回到开头，不管我们有没有指定host-dir，在libcontainer中实际上都是通过bind mount实现的。
