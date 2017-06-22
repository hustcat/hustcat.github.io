
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

* wait

```
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
```
