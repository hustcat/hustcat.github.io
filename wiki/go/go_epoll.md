
## 文件描述符

* netFD

`netFD`代表一个网络文件描述符，它包含`pollDesc`，指向`runtime`的网络文件描述符。
 
```
/// net/fd_unix.go
// Network file descriptor.
type netFD struct {
	// locking/lifetime of sysfd + serialize access to Read and Write methods
	fdmu fdMutex

	// immutable until Close
	sysfd       int /// OS fd
	family      int
	sotype      int
	isConnected bool
	net         string
	laddr       Addr
	raddr       Addr

	// wait server
	pd pollDesc
}

/// net/fd_poll_runtime.go
type pollDesc struct {
	runtimeCtx uintptr ///指针，指向PollDesc
}
```

* PollDesc

`PollDesc`为`runtime`中的网络文件描述符。

`runtime/netpoll.goc`

```
struct PollDesc
{
	PollDesc* link;	// in pollcache, protected by pollcache.Lock

	// The lock protects pollOpen, pollSetDeadline, pollUnblock and deadlineimpl operations.
	// This fully covers seq, rt and wt variables. fd is constant throughout the PollDesc lifetime.
	// pollReset, pollWait, pollWaitCanceled and runtime·netpollready (IO rediness notification)
	// proceed w/o taking the lock. So closing, rg, rd, wg and wd are manipulated
	// in a lock-free way by all operations.
	Lock;		// protectes the following fields
	uintptr	fd;
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

## 文件描述符初始化

```
// net/fd_poll_runtime.go
func (pd *pollDesc) Init(fd *netFD) error {
	serverInit.Do(runtime_pollServerInit)
	ctx, errno := runtime_pollOpen(uintptr(fd.sysfd))
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

//runtime/netpoll_epoll.c
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

## wait

```
//net/fd_poll_runtime.go
func (pd *pollDesc) Wait(mode int) error {
	res := runtime_pollWait(pd.runtimeCtx, mode)
	return convertErr(res)
}

func (pd *pollDesc) WaitRead() error {
	return pd.Wait('r')
}

func (pd *pollDesc) WaitWrite() error {
	return pd.Wait('w')
}

// runtime/netpoll.goc
func runtime_pollWait(pd *PollDesc, mode int) (err int) {
	err = checkerr(pd, mode);
	if(err == 0) {
		// As for now only Solaris uses level-triggered IO.
		if(Solaris)
			runtime·netpollarm(pd, mode);
		while(!netpollblock(pd, mode, false)) { ///等待IO ready
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
		runtime·park((bool(*)(G*, void*))blockcommit, gpp, "IO wait"); ////wait IO
	// be careful to not lose concurrent READY notification
	old = runtime·xchgp(gpp, nil);
	if(old > WAIT)
		runtime·throw("netpollblock: corrupted state");
	return old == READY;
}
```

## IO ready

```
/// runtime/netpoll_epoll.c
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
```

IO ready的`G`会被放到可运行队列，重新调度执行：

```
// runtime/netpoll.goc
// make pd ready, newly runnable goroutines (if any) are enqueued info gpp list
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
```
