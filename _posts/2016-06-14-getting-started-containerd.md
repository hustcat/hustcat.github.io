---
layout: post
title: Getting started containerd
date: 2016-06-14 20:22:30
categories: Container
tags: containerd
excerpt: Getting started containerd
---

## Getting started

### Compile runc

```sh
# git clone https://github.com/opencontainers/runc.git
# make
# ./runc -v
runc version 1.0.0-rc1
commit: 42dfd606437b538ffde4f0640d433916bee928e3
spec: 1.0.0-rc1
# cp runc  /usr/local/bin/runc
```

### Create OCI bundles

```sh
# mkdir -p /containers/redis/rootfs
# docker create --name tmpredis redis
9706a909bef2b856ab5512640728c25460b3bdba10bc242e1b55a8121d6e9b38
# docker export tmpredis | tar -C /containers/redis/rootfs -xf -
# docker rm tmpredis

# cat /containers/redis/config.json 
{
    "ociVersion": "0.4.0",
    "platform": {
        "os": "linux",
        "arch": "amd64"
    },
    "process": {
        "terminal": true,
        "user": {},
        "args": [
                "redis-server", "--bind", "0.0.0.0"
                ],
        "env": [
            "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
            "TERM=xterm"
        ],
        "cwd": "/",
        "capabilities": [
            "CAP_AUDIT_WRITE",
            "CAP_KILL",
            "CAP_NET_BIND_SERVICE"
        ],
        "rlimits": [
            {
                "type": "RLIMIT_NOFILE",
                "hard": 1024,
                "soft": 1024
            }
        ],
        "noNewPrivileges": true
    },
    "root": {
        "path": "rootfs",
        "readonly": true
    },
    "hostname": "runc",
    "mounts": [
        {
            "destination": "/proc",
            "type": "proc",
            "source": "proc"
        },
        {
            "destination": "/dev",
            "type": "tmpfs",
            "source": "tmpfs",
            "options": [
                "nosuid",
                "strictatime",
                "mode=755",
                "size=65536k"
            ]
        },
        {
            "destination": "/dev/pts",
            "type": "devpts",
            "source": "devpts",
            "options": [
                "nosuid",
                "noexec",
                "newinstance",
                "ptmxmode=0666",
                "mode=0620",
                "gid=5"
            ]
        },
        {
            "destination": "/dev/shm",
            "type": "tmpfs",
            "source": "shm",
            "options": [
                "nosuid",
                "noexec",
                "nodev",
                "mode=1777",
                "size=65536k"
            ]
        },
        {
            "destination": "/dev/mqueue",
            "type": "mqueue",
            "source": "mqueue",
            "options": [
                "nosuid",
                "noexec",
                "nodev"
            ]
        },
        {
            "destination": "/sys",
            "type": "sysfs",
            "source": "sysfs",
            "options": [
                "nosuid",
                "noexec",
                "nodev",
                "ro"
            ]
        },
        {
            "destination": "/sys/fs/cgroup",
            "type": "cgroup",
            "source": "cgroup",
            "options": [
                "nosuid",
                "noexec",
                "nodev",
                "relatime",
                "ro"
            ]
        }
    ],
    "hooks": {},
    "linux": {
        "resources": {
            "devices": [
                {
                    "allow": false,
                    "access": "rwm"
                }
            ]
        },
        "namespaces": [
            {
                "type": "pid"
            },
            {
                "type": "ipc"
            },
            {
                "type": "uts"
            },
            {
                "type": "mount"
            }
        ],
        "devices": null
    }
}
```

### Start containerd

```sh
# bin/containerd --shim /containers/bin/containerd-shim --debug
DEBU[0000] containerd: read past events                  count=0
DEBU[0000] containerd: supervisor running                cpus=8 memory=15776 runtime=runc runtimeArgs=[] stateDir=/run/containerd
DEBU[0000] containerd: grpc api on /run/containerd/containerd.sock
```

### Start container

```sh
# bin/ctr --debug containers start redis /containers/redis
# bin/ctr --debug containers list                         
ID                  PATH                STATUS              PROCESSES
redis               /containers/redis   running             init
```

Internal:

```sh
# tree /run/containerd/redis/  
/run/containerd/redis/
|-- init
|   |-- control
|   |-- exit
|   |-- log.json
|   |-- pid
|   |-- process.json
|   `-- shim-log.json
`-- state.json


# ps -ef --forest
root     31064  4698  0 19:43 pts/4    00:00:00  |       \_ bin/containerd --shim /containers/bin/containerd-shim --debug
root      4904 31064  0 20:04 pts/4    00:00:00  |           \_ /containers/bin/containerd-shim redis /containers/redis runc
root      4918  4904  0 20:04 pts/8    00:00:00  |               \_ redis-server 0.0.0.0:6379
```

## Implementation

### Start container

* containerd

```go
func (s *Supervisor) Start() error {
...
	go func() {
		for i := range s.tasks {
			s.handleTask(i)
		}
	}()
}


func (s *Supervisor) start(t *StartTask) error {
...
	task := &startTask{
		Err:           t.ErrorCh(),
		Container:     container,
		StartResponse: t.StartResponse,
		Stdin:         t.Stdin,
		Stdout:        t.Stdout,
		Stderr:        t.Stderr,
	}
	s.startTasks <- task
}


// Start runs a loop in charge of starting new containers
func (w *worker) Start() {
...
	for t := range w.s.startTasks {
		started := time.Now()
		process, err := t.Container.Start(t.CheckpointPath, runtime.NewStdio(t.Stdin, t.Stdout, t.Stderr))
...
}



///runtime/containerd.go
func (c *container) Start(checkpointPath string, s Stdio) (Process, error) {
	processRoot := filepath.Join(c.root, c.id, InitProcessID)
	if err := os.Mkdir(processRoot, 0755); err != nil {
		return nil, err
	}
	cmd := exec.Command(c.shim,
		c.id, c.bundle, c.runtime,
	)
	cmd.Dir = processRoot
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid: true,
	}
	spec, err := c.readSpec()
	if err != nil {
		return nil, err
	}
	config := &processConfig{
		checkpoint:  checkpointPath,
		root:        processRoot,
		id:          InitProcessID,
		c:           c,
		stdio:       s,
		spec:        spec,
		processSpec: specs.ProcessSpec(spec.Process),
	}
	p, err := newProcess(config)
	if err != nil {
		return nil, err
	}
	if err := c.createCmd(InitProcessID, cmd, p); err != nil { ///start containerd-shim
		return nil, err
	}
	return p, nil
}

```

* containerd-shim

Containerd-shim is a small shim that sits in front of a runtime implementation that allows it to be repartented to init and handle reattach from the caller.

```go
///containerd-shim/main.go
func start(log *os.File) error {
...
	if err := p.create(); err != nil {
		p.delete()
		return err
	}
...
}

func (p *process) create() error {
	cwd, err := os.Getwd()
	if err != nil {
		return err
	}
	logPath := filepath.Join(cwd, "log.json")
	args := append([]string{
		"--log", logPath,
		"--log-format", "json",
	}, p.state.RuntimeArgs...)
	if p.state.Exec {
		args = append(args, "exec",
			"-d",
			"--process", filepath.Join(cwd, "process.json"),
			"--console", p.consolePath,
		)
	} else if p.checkpoint != nil {
		args = append(args, "restore",
			"--image-path", p.checkpointPath,
			"--work-path", filepath.Join(p.checkpointPath, "criu.work", "restore-"+time.Now().Format(time.RFC3339)),
		)
		add := func(flags ...string) {
			args = append(args, flags...)
		}
		if p.checkpoint.Shell {
			add("--shell-job")
		}
		if p.checkpoint.TCP {
			add("--tcp-established")
		}
		if p.checkpoint.UnixSockets {
			add("--ext-unix-sk")
		}
		if p.state.NoPivotRoot {
			add("--no-pivot")
		}
		for _, ns := range p.checkpoint.EmptyNS {
			add("--empty-ns", ns)
		}

	} else {
		args = append(args, "create",
			"--bundle", p.bundle,
			"--console", p.consolePath,
		)
		if p.state.NoPivotRoot {
			args = append(args, "--no-pivot")
		}
	}
	args = append(args,
		"--pid-file", filepath.Join(cwd, "pid"),
		p.id,
	)
	cmd := exec.Command(p.runtime, args...) ///start runc
	cmd.Dir = p.bundle
	cmd.Stdin = p.stdio.stdin
	cmd.Stdout = p.stdio.stdout
	cmd.Stderr = p.stdio.stderr
	// Call out to setPDeathSig to set SysProcAttr as elements are platform specific
	cmd.SysProcAttr = setPDeathSig()

	if err := cmd.Start(); err != nil {
...
}
```

## Using runc

* Run container

```sh
# cd /containers/redis/
# runc run redis
1:M 15 Jun 03:10:37.101 # You requested maxclients of 10000 requiring at least 10032 max file descriptors.
1:M 15 Jun 03:10:37.101 # Server can't set maximum open files to 10032 because of OS error: Operation not permitted.
1:M 15 Jun 03:10:37.101 # Current maximum open files is 1024. maxclients has been reduced to 992 to compensate for low ulimit. If you need higher maxclients increase 'ulimit -n'.
…
```

* List container

```sh
# runc list
ID          PID         STATUS      BUNDLE              CREATED
redis       16476       running     /containers/redis   2016-06-15T03:10:37.097884439Z
# runc --debug state redis
{
  "ociVersion": "1.0.0-rc1",
  "id": "redis",
  "pid": 16476,
  "bundlePath": "/containers/redis",
  "rootfsPath": "/containers/redis/rootfs",
  "status": "running",
  "created": "2016-06-15T03:10:37.097884439Z"

# ls /run/runc/redis/
state.json


# ps -ef --forest
root     16467 17475  0 11:10 pts/6    00:00:00  |       \_ runc run redis
root     16476 16467  0 11:10 pts/8    00:00:00  |           \_ redis-server 0.0.0.0:6379


# runc kill redis
```

## Reference

* [containerd project](https://github.com/docker/containerd)
* [Creating OCI bundles](https://github.com/docker/containerd/blob/master/docs/bundle.md)
* [runc project](https://github.com/opencontainers/runc)
* [docker PR#20662: Containerd integration](https://github.com/docker/docker/pull/20662)

