---
layout: post
title: docker and terminal
date: 2015-11-03 18:16:30
categories: Linux
tags: docker terminal
excerpt: docker and terminal
---

对于交互式容器进程，比如shell，我们必须以-it创建容器：

```bash
#docker run -it ubuntu /bin/bash
```
那么问题来了，docker client与容器进程是没有直接关联的，它是如何得到容器进程的stdin/stdout，并进行交互的呢？

实际上，指定"-t"参数，docker就会创建pseudo-TTY设备。详细解释参考[这里](http://docs.docker.com/v1.4/reference/run/#foreground)。而pseudo-TTY则是client与容器进程进行交互的关键所在。

```go
	flTty             = cmd.Bool([]string{"t", "-tty"}, false, "Allocate a pseudo-TTY")
	flStdin           = cmd.Bool([]string{"i", "-interactive"}, false, "Keep STDIN open even if not attached")
...
	config := &Config{

		Tty:             *flTty,
		OpenStdin:       *flStdin,
```

先看看docker run背后的一些逻辑，实际上，client会发送下面一些请求到daemon：

```
POST /v1.13/containers/create
POST /v1.13/containers/$ID/attach?stderr=1&stdin=1&stdout=1&stream=1
POST /v1.13/containers/$ID/start
POST /v1.13/containers/$ID/resize?h=46&w=132
```

可以看到，在真正start容器之前，docker client会先发送attach请求。在讨论attach之前，我们先来看create的实现。

# create

Docker在启动容器时，会创建一个execdriver.Pipes对象，封装Container.StreamConfig，Container.StreamConfig保存容器在daemon进程中对应的stdin/stdout/stdout流对象，是docker client与容器之间stdin/stdout/stderr传输数据的中转站。当docker client attach到容器时，client由于只能与daemon通信，得不到容器的stdin/stdout/stderr对象：

* Container.StreamConfig

```go
//Container.go
type Container struct {
...
	command *execdriver.Command
	StreamConfig
...


type StreamConfig struct {
	stdout    *broadcastwriter.BroadcastWriter
	stderr    *broadcastwriter.BroadcastWriter
	stdin     io.ReadCloser
	stdinPipe io.WriteCloser
}
```

* execdriver.Pipes

```go
//daemon/execdriver/pipes.go
// Pipes is a wrapper around a containers output for
// stdin, stdout, stderr
type Pipes struct {
	Stdin          io.ReadCloser
	Stdout, Stderr io.Writer
}


// Start starts the containers process and monitors it according to the restart policy
func (m *containerMonitor) Start() error {
...
	pipes := execdriver.NewPipes(m.container.stdin, m.container.stdout, m.container.stderr, m.container.Config.OpenStdin)
...
	exitStatus, err = m.container.daemon.Run(m.container, pipes, m.callback);
```

* create pseudo tty for container

然后创建pseudo tty设备，Container.stdin拷贝到master设备，master的数据流拷贝到Container.stdout。

```go
////native/driver.go：
func (d *driver) Run(c *execdriver.Command, pipes *execdriver.Pipes, startCallback execdriver.StartCallback) (int, error) {
	// take the Command and populate the libcontainer.Config from it
	container, err := d.createContainer(c)
	if err != nil {
		return -1, err
	}

	var term execdriver.Terminal

	if c.ProcessConfig.Tty {
		term, err = NewTtyConsole(&c.ProcessConfig, pipes)
	} else {
		term, err = execdriver.NewStdConsole(&c.ProcessConfig, pipes)
	}
	if err != nil {
		return -1, err
	}
	c.ProcessConfig.Terminal = term

/////execdriver/driver.go
// Terminal in an interface for drivers to implement
// if they want to support Close and Resize calls from
// the core
type Terminal interface {
	io.Closer
	Resize(height, width int) error
}

type TtyTerminal interface {
	Master() *os.File
}

///execdriver/native/driver.go

type TtyConsole struct {
	MasterPty *os.File
}

//创建pseudo tty
func NewTtyConsole(processConfig *execdriver.ProcessConfig, pipes *execdriver.Pipes) (*TtyConsole, error) {
	ptyMaster, console, err := consolepkg.CreateMasterAndConsole()
	if err != nil {
		return nil, err
	}

	tty := &TtyConsole{
		MasterPty: ptyMaster,
	}

	if err := tty.AttachPipes(&processConfig.Cmd, pipes); err != nil {
		tty.Close()
		return nil, err
	}

	processConfig.Console = console

	return tty, nil
}
```

* associate Container.StreamConfig with execdriver.Pipes

```go
//与容器的stdin/stdout关联
func (t *TtyConsole) AttachPipes(command *exec.Cmd, pipes *execdriver.Pipes) error {
	go func() {
		if wb, ok := pipes.Stdout.(interface {
			CloseWriters() error
		}); ok {
			defer wb.CloseWriters()
		}

		io.Copy(pipes.Stdout, t.MasterPty)
	}()

	if pipes.Stdin != nil {
		go func() {
			io.Copy(t.MasterPty, pipes.Stdin)

			pipes.Stdin.Close()
		}()
	}

	return nil
}

//daemon/execdriver/driver.go
// Describes a process that will be run inside a container.
type ProcessConfig struct {
	exec.Cmd `json:"-"`

	Privileged bool     `json:"privileged"`
	User       string   `json:"user"`
	Tty        bool     `json:"tty"`
	Entrypoint string   `json:"entrypoint"`
	Arguments  []string `json:"arguments"`
	Terminal   Terminal `json:"-"` // standard or tty terminal, tty master
	Console    string   `json:"-"` // dev/console path, tty slave 
}
```

* dockerinit如何使用pty slave?

dockerinit会将0/1/2重定向到slave设备。这样，容器内的进程的stdin/stdout/stdout都重定向到slave设备。

```go
func Init(container *libcontainer.Config, uncleanRootfs, consolePath string, syncPipe *syncpipe.SyncPipe, args []string) (err error) {

...
	if consolePath != "" {
		if err := console.OpenAndDup(consolePath); err != nil {
			return err
		}
	}
	if _, err := syscall.Setsid(); err != nil {
		return fmt.Errorf("setsid %s", err)
	}
	if consolePath != "" {
		if err := system.Setctty(); err != nil {
			return fmt.Errorf("setctty %s", err)
		}
	}


func OpenAndDup(consolePath string) error {
	slave, err := OpenTerminal(consolePath, syscall.O_RDWR)
	if err != nil {
		return fmt.Errorf("open terminal %s", err)
	}

	if err := syscall.Dup2(int(slave.Fd()), 0); err != nil {
		return err
	}

	if err := syscall.Dup2(int(slave.Fd()), 1); err != nil {
		return err
	}

	return syscall.Dup2(int(slave.Fd()), 2)
}
```

到这里，就可以大概明白了，docker daemon进程持有pseudo tty master设备，容器进程持有slave设备，daemon与容器进程就可以通过tty进行数据传输了。剩下的就是docker client如何与daemon进行关联了。

# 测试

```bash
# docker run --privileged -it ubuntu /bin/bash
root@3134a81a12e5:/# tty
/dev/console
root@3134a81a12e5:/# ls /proc/self/fd/* -l
ls: cannot access /proc/self/fd/255: No such file or directory
ls: cannot access /proc/self/fd/3: No such file or directory
lrwx------ 1 root root 64 Nov  3 08:45 /proc/self/fd/0 -> /4
lrwx------ 1 root root 64 Nov  3 08:45 /proc/self/fd/1 -> /4
lrwx------ 1 root root 64 Nov  3 08:45 /proc/self/fd/2 -> /4
root@3134a81a12e5:/# ls -lh /dev/console 
crw------- 1 root root 136, 4 Nov  3 08:48 /dev/console
```

查看一下几个相关的进程：

```bash
[root@host]#ps
root      5442  7233  0 16:44 pts/1    00:00:00 docker run --privileged -it ubuntu /bin/bash
root      5498 18307  0 16:44 pts/4    00:00:00 /bin/bash
root     18307     1  0 14:43 pts/2    00:00:42 /usr/bin/docker -d -g /data/docker/
```

可以看到容器的bash的0/1/2指向fd(4)（对应slave设备）：

```bash
[root@host]# ls /proc/5498/fd/* -l
lrwx------ 1 root root 64 Nov  3 16:45 /proc/5498/fd/0 -> /4
lrwx------ 1 root root 64 Nov  3 16:45 /proc/5498/fd/1 -> /4
lrwx------ 1 root root 64 Nov  3 16:45 /proc/5498/fd/2 -> /4
lrwx------ 1 root root 64 Nov  3 16:45 /proc/5498/fd/255 -> /4
```

docker client的0/1/2：

```bash
[root@host]# ls /proc/5442/fd/* -l
lrwx------ 1 root root 64 Nov  3 16:44 /proc/5442/fd/0 -> /dev/pts/1
lrwx------ 1 root root 64 Nov  3 16:44 /proc/5442/fd/1 -> /dev/pts/1
lrwx------ 1 root root 64 Nov  3 16:44 /proc/5442/fd/2 -> /dev/pts/1
```

另外，容器的控制终端为/dev/pts/4，实际上即为容器内部看到的/dev/console：

```bash
[root@host]# ls -l /dev/pts/4 
crw------- 1 root root 136, 4 Nov  3 17:41 /dev/pts/4
```

# attach的原理

attach的基本原理就是将容器的pseudo master与client、damon之间的http关联，这样，client的输入就会通过http连接发到容器的stdin、容器的stdout也会通过http连接返回给client。

attach的时候，daemon首先会将job.Stdin/job.Stdout/job.Stderr指向http底层连接。这样，client发过来的数据就会转到job.Stdin，job.Stdout的输出就会通过http连接返回给client。

```go
//api/server/server.go
func postContainersAttach(eng *engine.Engine, version version.Version, w http.ResponseWriter, r *http.Request, vars map[string]string) error {
...
	inStream, outStream, err := hijackServer(w)
...
	job = eng.Job("attach", vars["name"])
	job.Setenv("logs", r.Form.Get("logs"))
	job.Setenv("stream", r.Form.Get("stream"))
	job.Setenv("stdin", r.Form.Get("stdin"))
	job.Setenv("stdout", r.Form.Get("stdout"))
	job.Setenv("stderr", r.Form.Get("stderr"))
	job.Stdin.Add(inStream)
	job.Stdout.Add(outStream)
	job.Stderr.Set(errStream)
...


func hijackServer(w http.ResponseWriter) (io.ReadCloser, io.Writer, error) {
	conn, _, err := w.(http.Hijacker).Hijack()
	if err != nil {
		return nil, nil, err
	}
	// Flush the options to make sure the client sets the raw mode
	conn.Write([]byte{})
	return conn, conn, nil
}
```

接下来，需要将容器的stdin/stdout/stderr(即Container.StreamConfig)与job.Stdin/job.Stdout/job.Stdout关联。
从docker create的实现可以看到，容器的stdin/stdout/stderr在docker daemon中保存在数据结构Container.StreamConfig。所以，只需要将container.StreamConfig与job.Stdin/job.Stdout/job.Stderr关联即可。

```go
///attach.go
func (daemon *Daemon) ContainerAttach(job *engine.Job) engine.Status {
...
	//stream
	if stream {
		var (
			cStdin           io.ReadCloser
			cStdout, cStderr io.Writer
			cStdinCloser     io.Closer
		)

		if stdin {
			r, w := io.Pipe()
			go func() {
				defer w.Close()
				defer log.Debugf("Closing buffered stdin pipe")
				io.Copy(w, job.Stdin)
			}()
			cStdin = r
			cStdinCloser = job.Stdin
		}
		if stdout {
			cStdout = job.Stdout
		}
		if stderr {
			cStderr = job.Stderr
		}
		///将container.StreamConfig与job.Stdin/job.Stdout/job.Stderr关联
		<-daemon.Attach(&container.StreamConfig, container.Config.OpenStdin, container.Config.StdinOnce, container.Config.Tty, cStdin, cStdinCloser, cStdout, cStderr)
```

另一方面，从container start的逻辑可以看出，docker daemon会将Container.StreamConfig与pseudo tty的master设备关联。这样，pty master与client的连接。

# 总结

attach容器后，docker client与容器进程交互的整体流程大致如下：

![](/assets/2015-11-04-docker-and-terminal.jpg)

实际上，如果想docker client attach容器进程，"-t"是必须参数，不管有没有指定"-i"，只有指定了"-t"，才会创建tty设备。另外，值得一起的是，参考数"-a"还可以指定attach某个特定标准流，比如：

```bash
# docker run -it -a stdin centos:centos6 /bin/bash                              
96292e2d3a4812f738be0177873fa52e49323f42fe53c4e148e8f070985e1866
# docker run -it -a stdout centos:centos6 /bin/bash        
bash-4.1# echo hello
hehe  ------ # echo hehe > /dev/pts/12 ---in another host terminal 
```
如果我们只指定stdin，client则看不到stdout的输出；如果只指定stdout，client则无法进行输入，但是可以看到输出。

>  === begin update 2016/12/08 ===

实际上，如果不指定`-t`参数，也可attach到容器：

```sh
# docker run -i  --rm dbyin/busybox:latest /bin/sh
echo hello
hello
ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
51: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue 
    link/ether 02:42:ac:11:00:03 brd ff:ff:ff:ff:ff:ff
    inet 172.17.0.3/16 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::42:acff:fe11:3/64 scope link 
       valid_lft forever preferred_lft forever
```

这时docker直接将docker client的`stdio`通过pipe拷贝到容器的`stdio`，或者说容器的`stdio`即为pipe.Stdin(即http connection):

```sh
# ls /proc/36753/fd* -lh
/proc/36753/fd:
total 0
lr-x------ 1 root root 64 Dec  8 16:41 0 -> pipe:[7515032]
l-wx------ 1 root root 64 Dec  8 16:41 1 -> pipe:[7515033]
l-wx------ 1 root root 64 Dec  8 16:41 2 -> pipe:[7515034]
```

>  === end update 2016/12/08 ===

# 相关资料
* [man7: ptmx, pts - pseudoterminal master and slave](http://man7.org/linux/man-pages/man4/pts.4.html)
* [docker doc: docker attach](http://docs.docker.com/v1.4/reference/commandline/cli/#attach)
