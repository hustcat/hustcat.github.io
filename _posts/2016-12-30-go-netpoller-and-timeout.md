---
layout: post
title: The Go netpoller and timeout
date: 2016-12-30 18:00:30
categories: 编程语言
tags: golang
excerpt: The Go netpoller and timeout
---

## 数据结构

* netFD

`netFD`是pkg的网络文件描述符。

```go
//net.go
type conn struct {
	fd *netFD
}

//net/fd_unix.go
// Network file descriptor.
type netFD struct {
	// locking/lifetime of sysfd + serialize access to Read and Write methods
	fdmu fdMutex

	// immutable until Close
	sysfd       int ///OS socket fd
	family      int
	sotype      int
	isConnected bool
	net         string
	laddr       Addr
	raddr       Addr

	// wait server
	pd pollDesc
}

//net/fd_poll_runtime.go
type pollDesc struct {
	runtimeCtx uintptr ///point to runtime PollDesc
}
```

* PollDesc

`PollDesc`代表运行时的文件描述符.

```go
///runtime/netpoll.goc
struct PollDesc
{
	PollDesc* link;	// in pollcache, protected by pollcache.Lock

	// The lock protects pollOpen, pollSetDeadline, pollUnblock and deadlineimpl operations.
	// This fully covers seq, rt and wt variables. fd is constant throughout the PollDesc lifetime.
	// pollReset, pollWait, pollWaitCanceled and runtime·netpollready (IO rediness notification)
	// proceed w/o taking the lock. So closing, rg, rd, wg and wd are manipulated
	// in a lock-free way by all operations.
	Lock;		// protectes the following fields
	uintptr	fd;  //OS socket fd, 对应netFD.sysfd
	bool	closing;
	uintptr	seq;	// protects from stale timers and ready notifications
	G*	rg;	// READY, WAIT, G waiting for read or nil
	Timer	rt;	// read deadline timer (set if rt.fv != nil)
	int64	rd;	// read deadline
	G*	wg;	// READY, WAIT, G waiting for write or nil
	Timer	wt;	// write deadline timer
	int64	wd;	// write deadline
	void*	user;	// user settable cookie
};
```

## Read

从网络fd读取数据，goroutine会一直Read，直到发生`EAGAIN`，则将当前goroutine加到wait队列:

```go
//net/fd_unix.go
func (fd *netFD) Read(p []byte) (n int, err error) {
	if err := fd.readLock(); err != nil {
		return 0, err
	}
	defer fd.readUnlock()
	if err := fd.pd.PrepareRead(); err != nil { ///检查有没有错误
		return 0, &OpError{"read", fd.net, fd.raddr, err}
	}
	for { ///一直Read，直到发生EAGAIN，则将当前goroutine加到wait队列
		n, err = syscall.Read(int(fd.sysfd), p)
		if err != nil {
			n = 0
			if err == syscall.EAGAIN {
				if err = fd.pd.WaitRead(); err == nil { ///等待epoll唤醒
					continue
				}
			}
		}
		err = chkReadErr(n, err, fd)
		break
	}
	if err != nil && err != io.EOF {
		err = &OpError{"read", fd.net, fd.raddr, err}
	}
	return
}
```


```go
//net/fd_poll_runtime.go
func (pd *pollDesc) Wait(mode int) error {
	res := runtime_pollWait(pd.runtimeCtx, mode)
	return convertErr(res)
}
```


* runtime_pollWait

```go
//runtime/netpoll.goc
func runtime_pollWait(pd *PollDesc, mode int) (err int) {
	err = checkerr(pd, mode);
	if(err == 0) {
		// As for now only Solaris uses level-triggered IO.
		if(Solaris)
			runtime·netpollarm(pd, mode);
		while(!netpollblock(pd, mode, false)) {
			err = checkerr(pd, mode);
			if(err != 0)
				break;
			// Can happen if timeout has fired and unblocked us,
			// but before we had a chance to run, timeout has been reset.
			// Pretend it has not happened and retry.
		}
	}
}

// returns true if IO is ready, or false if timedout or closed
// waitio - wait only for completed IO, ignore errors
static bool
netpollblock(PollDesc *pd, int32 mode, bool waitio)
{
	G **gpp, *old;

	gpp = &pd->rg;
	if(mode == 'w')
		gpp = &pd->wg;

	// set the gpp semaphore to WAIT
	for(;;) {
		old = *gpp;
		if(old == READY) {
			*gpp = nil;
			return true;
		}
		if(old != nil)
			runtime·throw("netpollblock: double wait");
		if(runtime·casp(gpp, nil, WAIT))
			break;
	}

	// need to recheck error states after setting gpp to WAIT
	// this is necessary because runtime_pollUnblock/runtime_pollSetDeadline/deadlineimpl
	// do the opposite: store to closing/rd/wd, membarrier, load of rg/wg
	if(waitio || checkerr(pd, mode) == 0)
		runtime·park((bool(*)(G*, void*))blockcommit, gpp, "IO wait"); ////阻塞当前的goroutine
	// be careful to not lose concurrent READY notification
	old = runtime·xchgp(gpp, nil);
	if(old > WAIT)
		runtime·throw("netpollblock: corrupted state");
	return old == READY;
}

static intgo
checkerr(PollDesc *pd, int32 mode)
{
	if(pd->closing)
		return 1;  // errClosing
	if((mode == 'r' && pd->rd < 0) || (mode == 'w' && pd->wd < 0))
		return 2;  // errTimeout
	return 0;
}
```


## sysmon

`sysmon`OS线程会定期检查epoll事件，并将ready的G加入到全局队列(值得注意的是并没有直接调用`runtime·ready`):

```
//runtime/proc.c
static void
sysmon(void)
{
///..
		// poll network if not polled for more than 10ms
		lastpoll = runtime·atomicload64(&runtime·sched.lastpoll);
		now = runtime·nanotime();
		if(lastpoll != 0 && lastpoll + 10*1000*1000 < now) {
			runtime·cas64(&runtime·sched.lastpoll, lastpoll, now);
			gp = runtime·netpoll(false);  // non-blocking
			if(gp) {
				// Need to decrement number of idle locked M's
				// (pretending that one more is running) before injectglist.
				// Otherwise it can lead to the following situation:
				// injectglist grabs all P's but before it starts M's to run the P's,
				// another M returns from syscall, finishes running its G,
				// observes that there is no work to do and no other running M's
				// and reports deadlock.
				incidlelocked(-1);
				injectglist(gp); // 加到全局队列
				incidlelocked(1);
			}
		}
}

```

* epoll_wait

```go
//runtime/netpoll_epoll.c
// polls for ready network connections
// returns list of goroutines that become runnable
G*
runtime·netpoll(bool block)
{
	static int32 lasterr;
	EpollEvent events[128], *ev;
	int32 n, i, waitms, mode;
	G *gp;

	if(epfd == -1)
		return nil;
	waitms = -1;
	if(!block)
		waitms = 0;
retry:
	n = runtime·epollwait(epfd, events, nelem(events), waitms);
	if(n < 0) {
		if(n != -EINTR && n != lasterr) {
			lasterr = n;
			runtime·printf("runtime: epollwait on fd %d failed with %d\n", epfd, -n);
		}
		goto retry;
	}
	gp = nil;
	for(i = 0; i < n; i++) {
		ev = &events[i];
		if(ev->events == 0)
			continue;
		mode = 0;
		if(ev->events & (EPOLLIN|EPOLLRDHUP|EPOLLHUP|EPOLLERR))
			mode += 'r';
		if(ev->events & (EPOLLOUT|EPOLLHUP|EPOLLERR))
			mode += 'w';
		if(mode)
			runtime·netpollready(&gp, (void*)ev->data, mode);
	}
	if(block && gp == nil)
		goto retry;
	return gp;
}

// Injects the list of runnable G's into the scheduler.
// Can run concurrently with GC.
static void
injectglist(G *glist)
{
	int32 n;
	G *gp;

	if(glist == nil)
		return;
	runtime·lock(&runtime·sched);
	for(n = 0; glist; n++) {
		gp = glist;
		glist = gp->schedlink;
		gp->status = Grunnable;
		globrunqput(gp);
	}
	runtime·unlock(&runtime·sched);

	for(; n && runtime·sched.npidle; n--)
		startm(nil, false);
}
```


```go
///runtime/netpoll.goc
// make pd ready, newly runnable goroutines (if any) are enqueued info gpp list
void
runtime·netpollready(G **gpp, PollDesc *pd, int32 mode)
{
	G *rg, *wg;

	rg = wg = nil;
	if(mode == 'r' || mode == 'r'+'w')
		rg = netpollunblock(pd, 'r', true);
	if(mode == 'w' || mode == 'r'+'w')
		wg = netpollunblock(pd, 'w', true);
	if(rg) {
		rg->schedlink = *gpp;
		*gpp = rg;
	}
	if(wg) {
		wg->schedlink = *gpp;
		*gpp = wg;
	}
}

static G*
netpollunblock(PollDesc *pd, int32 mode, bool ioready)
{
	G **gpp, *old, *new;

	gpp = &pd->rg;
	if(mode == 'w')
		gpp = &pd->wg;

	for(;;) {
		old = *gpp;
		if(old == READY)
			return nil;
		if(old == nil && !ioready) {
			// Only set READY for ioready. runtime_pollWait
			// will check for timeout/cancel before waiting.
			return nil;
		}
		new = nil;
		if(ioready)
			new = READY;
		if(runtime·casp(gpp, old, new))
			break;
	}
	if(old > WAIT)
		return old;  // must be G*
	return nil;
}
```


## SetReadDeadline

`SetReadDeadline`用于实现Go的网络IO超时原语，它会给`netFD`创建对应的IO定时器，当定时器超时，runtime会调用`runtime·ready`唤醒对应进行`Read/Write`的goroutine，如果对应的goroutine处于等待的状态(默认情况下deadline为0，不会创建定时器)。

Go并没有使用`epoll_wait`实现IO的超时，而是通过`Set[Read|Write]Deadline(time.Time)`对每个`netFD`设置超时。

当`SetDeadline`设置的定时器超时后，在超时处理函数中，会删除该定时器；而且，每次收到或者发送数据时，也不会reset该定时器。所以，每次`Read/Write`操作之前，都需要调用该函数:

```go
// A net.Conn that sets a deadline for every Read or Write operation
type TimeoutConn struct {
	net.Conn
	timeout time.Duration
}

func (c *TimeoutConn) Read(b []byte) (int, error) {
	if c.timeout > 0 {
		err := c.Conn.SetReadDeadline(time.Now().Add(c.timeout))
		if err != nil {
			return 0, err
		}
	}
	return c.Conn.Read(b)
}
```


```go
// SetReadDeadline implements the Conn SetReadDeadline method.
func (c *conn) SetReadDeadline(t time.Time) error {
	if !c.ok() {
		return syscall.EINVAL
	}
	return c.fd.setReadDeadline(t)
}
```


```go
///net/fd_poll_runtime.go
func (fd *netFD) setReadDeadline(t time.Time) error {
	return setDeadlineImpl(fd, t, 'r')
}

func (fd *netFD) setWriteDeadline(t time.Time) error {
	return setDeadlineImpl(fd, t, 'w')
}

func setDeadlineImpl(fd *netFD, t time.Time, mode int) error {
	d := runtimeNano() + int64(t.Sub(time.Now()))
	if t.IsZero() {
		d = 0
	}
	if err := fd.incref(); err != nil {
		return err
	}
	runtime_pollSetDeadline(fd.pd.runtimeCtx, d, mode)
	fd.decref()
	return nil
}
```


```go
func runtime_pollSetDeadline(pd *PollDesc, d int64, mode int) {
	G *rg, *wg;

	runtime·lock(pd);
	if(pd->closing) {
		runtime·unlock(pd);
		return;
	}
	pd->seq++;  // invalidate current timers
	// Reset current timers.
	if(pd->rt.fv) {
		runtime·deltimer(&pd->rt);
		pd->rt.fv = nil;
	}
	if(pd->wt.fv) {
		runtime·deltimer(&pd->wt);
		pd->wt.fv = nil;
	}
	// Setup new timers.
	if(d != 0 && d <= runtime·nanotime())
		d = -1;
	if(mode == 'r' || mode == 'r'+'w')
		pd->rd = d;
	if(mode == 'w' || mode == 'r'+'w')
		pd->wd = d;
	if(pd->rd > 0 && pd->rd == pd->wd) {
		pd->rt.fv = &deadlineFn;
		pd->rt.when = pd->rd;
		// Copy current seq into the timer arg.
		// Timer func will check the seq against current descriptor seq,
		// if they differ the descriptor was reused or timers were reset.
		pd->rt.arg.type = (Type*)pd->seq;
		pd->rt.arg.data = pd;
		runtime·addtimer(&pd->rt);
	} else {
		if(pd->rd > 0) {
			pd->rt.fv = &readDeadlineFn;
			pd->rt.when = pd->rd;
			pd->rt.arg.type = (Type*)pd->seq;
			pd->rt.arg.data = pd;
			runtime·addtimer(&pd->rt);
		}
		if(pd->wd > 0) {
			pd->wt.fv = &writeDeadlineFn;
			pd->wt.when = pd->wd;
			pd->wt.arg.type = (Type*)pd->seq;
			pd->wt.arg.data = pd;
			runtime·addtimer(&pd->wt);
		}
	}
	// If we set the new deadline in the past, unblock currently pending IO if any.
	rg = nil;
	runtime·atomicstorep(&wg, nil);  // full memory barrier between stores to rd/wd and load of rg/wg in netpollunblock
	if(pd->rd < 0)
		rg = netpollunblock(pd, 'r', false);
	if(pd->wd < 0)
		wg = netpollunblock(pd, 'w', false);
	runtime·unlock(pd);
	if(rg)
		runtime·ready(rg);
	if(wg)
		runtime·ready(wg);
}

static FuncVal deadlineFn	= {(void(*)(void))deadline};
static FuncVal readDeadlineFn	= {(void(*)(void))readDeadline};
static FuncVal writeDeadlineFn	= {(void(*)(void))writeDeadline};


static void
readDeadline(int64 now, Eface arg)
{
	deadlineimpl(now, arg, true, false);
}

static void
deadlineimpl(int64 now, Eface arg, bool read, bool write)
{
	PollDesc *pd;
	uint32 seq;
	G *rg, *wg;

	USED(now);
	pd = (PollDesc*)arg.data;
	// This is the seq when the timer was set.
	// If it's stale, ignore the timer event.
	seq = (uintptr)arg.type;
	rg = wg = nil;
	runtime·lock(pd);
	if(seq != pd->seq) {
		// The descriptor was reused or timers were reset.
		runtime·unlock(pd);
		return;
	}
	if(read) {
		if(pd->rd <= 0 || pd->rt.fv == nil)
			runtime·throw("deadlineimpl: inconsistent read deadline");
		pd->rd = -1;
		runtime·atomicstorep(&pd->rt.fv, nil);  // full memory barrier between store to rd and load of rg in netpollunblock
		rg = netpollunblock(pd, 'r', false);
	}
	if(write) {
		if(pd->wd <= 0 || (pd->wt.fv == nil && !read))
			runtime·throw("deadlineimpl: inconsistent write deadline");
		pd->wd = -1;
		runtime·atomicstorep(&pd->wt.fv, nil);  // full memory barrier between store to wd and load of wg in netpollunblock
		wg = netpollunblock(pd, 'w', false);
	}
	runtime·unlock(pd);
	if(rg)
		runtime·ready(rg);
	if(wg)
		runtime·ready(wg);
}

```


## 初始化操作

* epoll init

```go
///runtime/netpoll_epoll.c
static int32 epfd = -1;  // epoll descriptor

void
runtime·netpollinit(void)
{
	epfd = runtime·epollcreate1(EPOLL_CLOEXEC);
	if(epfd >= 0)
		return;
	epfd = runtime·epollcreate(1024);
	if(epfd >= 0) {
		runtime·closeonexec(epfd);
		return;
	}
	runtime·printf("netpollinit: failed to create descriptor (%d)\n", -epfd);
	runtime·throw("netpollinit: failed to create descriptor");
}
```

* netFD init

`runtime`创建`socket fd`后，会将其加入到epoll:

```go
//net/fd_unix.go
func (fd *netFD) init() error {
	if err := fd.pd.Init(fd); err != nil {
		return err
	}
	return nil
}

//net/fd_poll_runtime.go
func (pd *pollDesc) Init(fd *netFD) error {
	serverInit.Do(runtime_pollServerInit)
	ctx, errno := runtime_pollOpen(uintptr(fd.sysfd)) ///add to epoll
	if errno != 0 {
		return syscall.Errno(errno)
	}
	pd.runtimeCtx = ctx
	return nil
}

//runtime/netpoll.goc
func runtime_pollOpen(fd uintptr) (pd *PollDesc, errno int) {
	pd = allocPollDesc();
	runtime·lock(pd);
	if(pd->wg != nil && pd->wg != READY)
		runtime·throw("runtime_pollOpen: blocked write on free descriptor");
	if(pd->rg != nil && pd->rg != READY)
		runtime·throw("runtime_pollOpen: blocked read on free descriptor");
	pd->fd = fd;
	pd->closing = false;
	pd->seq++;
	pd->rg = nil;
	pd->rd = 0;
	pd->wg = nil;
	pd->wd = 0;
	runtime·unlock(pd);

	errno = runtime·netpollopen(fd, pd);
}

int32
runtime·netpollopen(uintptr fd, PollDesc *pd)
{
	EpollEvent ev;
	int32 res;

	ev.events = EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLET;
	ev.data = (uint64)pd;
	res = runtime·epollctl(epfd, EPOLL_CTL_ADD, (int32)fd, &ev);
	return -res;
}
```

## 总结

`SetReadDeadline`用于实现Go的网络IO超时原语，它会给`netFD`创建对应的IO定时器，当定时器超时，runtime会调用`runtime·ready`唤醒对应进行`Read/Write`的goroutine，如果对应的goroutine处于等待的状态(默认情况下deadline为0，不会创建定时器)。

Go并没有使用`epoll_wait`实现IO的超时，而是通过`Set[Read|Write]Deadline(time.Time)`对每个`netFD`设置超时。

当`SetDeadline`设置的定时器超时后，在超时处理函数中，会删除该定时器；而且，每次收到或者发送数据时，也不会reset该定时器。所以，每次`Read/Write`操作之前，都需要调用该函数。

## Reference

* [The Go netpoller](https://morsmachine.dk/netpoller)
* [The complete guide to Go net/http timeouts](https://blog.cloudflare.com/the-complete-guide-to-golang-net-http-timeouts/)
