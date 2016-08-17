---
layout: post
title: Dive into channel in go
date: 2016-08-17 10:00:30
categories: 编程语言
tags: golang
excerpt: Dive into channel in go
---

在理解channel之前，最好先理解[goroutine的实现](http://hustcat.github.io/dive-into-goroutine/)。

## 数据结构

channel在runtime中实际上对应一个环形缓冲区(circular buffer)：

```c
// The garbage collector is assuming that Hchan can only contain pointers into the stack
// and cannot contain pointers into the heap.
struct	Hchan
{
	uintgo	qcount;			// total data in the q, 元素数量
	uintgo	dataqsiz;		// size of the circular q, 缓冲区大小(以元素为单位)
	uint16	elemsize;		// 数据元素的大小
	uint16	pad;			// ensures proper alignment of the buffer that follows Hchan in memory
	bool	closed;
	Type*	elemtype;		// element type,数据元素类型
	uintgo	sendx;			// send index
	uintgo	recvx;			// receive index
	WaitQ	recvq;			// list of recv waiters ///等待接收的G队列
	WaitQ	sendq;			// list of send waiters ///等待发送的G队列
	Lock;
};
```

等待队列:

```c
struct	SudoG
{
	G*	g;
	uint32*	selectdone;
	SudoG*	link;
	int64	releasetime;
	byte*	elem;		// data element
};

struct	WaitQ
{
	SudoG*	first;
	SudoG*	last;
};
```

`WaitQ`为等待(发送/接收)队列，`SudoG`对G做了一层封装。

## 创建channel

创建channel时，需要指定缓冲区的大小(以元素为单位)，对于同步channel，大小为0：

```c
// runtime/chan.goc
static Hchan*
makechan(ChanType *t, int64 hint)
{
	Hchan *c;
	Type *elem;

	elem = t->elem;
	/// 数据类型的大小不能超过64K
	// compiler checks this but be safe.
	if(elem->size >= (1<<16))
		runtime·throw("makechan: invalid channel element type");

    /// 分配缓冲区内存,数据存储在Hchan之后
	// allocate memory in one call
	c = (Hchan*)runtime·mallocgc(sizeof(*c) + hint*elem->size, (uintptr)t | TypeInfo_Chan, 0);
	c->elemsize = elem->size;
	c->elemtype = elem;
	c->dataqsiz = hint;

	return c;
}
```

## Send

```c
static bool
chansend(ChanType *t, Hchan *c, byte *ep, bool block, void *pc)
{
	SudoG *sg;
	SudoG mysg;
	G* gp;
...
```

参数`ep`为待发送的数据，block表示是否是阻塞模式。

(1) 如果channel为nil，则陷入阻塞:

```c
	if(c == nil) {
		USED(t);
		if(!block)
			return false;
		runtime·park(nil, nil, "chan send (nil chan)");
		return false;  // not reached
	}
```

(2) 如果channel已经close，则报错:

```c
	if(c->closed)
		goto closed;
...
closed:
	runtime·unlock(c);
	runtime·panicstring("send on closed channel");
	return false;  // not reached
}
```

(3) 如果是同步channel，如果有接收者，则拷贝数据，并唤醒接收者；否则，将自己加入sendq等待队列，并陷入阻塞:

```c
	sg = dequeue(&c->recvq);
	if(sg != nil) {
		if(raceenabled)
			racesync(c, sg);
		runtime·unlock(c);

		gp = sg->g;
		gp->param = sg;
		if(sg->elem != nil)
			c->elemtype->alg->copy(c->elemsize, sg->elem, ep);
		if(sg->releasetime)
			sg->releasetime = runtime·cputicks();
		runtime·ready(gp);
		return true;
	}

	if(!block) {
		runtime·unlock(c);
		return false;
	}

	mysg.elem = ep;
	mysg.g = g;
	mysg.selectdone = nil;
	g->param = nil;
	enqueue(&c->sendq, &mysg);
	runtime·parkunlock(c, "chan send"); ///陷入阻塞
```

(4) 如果是异步发送(`c->dataqsiz > 0`)，如果缓冲未满，则将数据拷贝到缓冲区，并唤醒接收者(如果c->recvq不为空)；否则，将自己加入sendq等待队列，并陷入阻塞:

```c
	/// 缓冲区已满
	if(c->qcount >= c->dataqsiz) {
		if(!block) {
			runtime·unlock(c);
			return false;
		}
		mysg.g = g;
		mysg.elem = nil;
		mysg.selectdone = nil;
		enqueue(&c->sendq, &mysg);
		runtime·parkunlock(c, "chan send");

		runtime·lock(c);
		goto asynch;
	}
```

## Receive

```c
static bool
chanrecv(ChanType *t, Hchan* c, byte *ep, bool block, bool *received)
{
	SudoG *sg;
	SudoG mysg;
	G *gp;
...
```

参考ep保存接收到的数据，received表示是否接收成功。

(1) 如果channel为nil，则陷入阻塞:

```c
	if(c == nil) {
		USED(t);
		if(!block)
			return false;
		runtime·park(nil, nil, "chan receive (nil chan)");
		return false;  // not reached
	}
```

(2) 如果channel已经close，则直接返回:

```c
	if(c->closed)
		goto closed;
...
closed:
	if(ep != nil)
		c->elemtype->alg->copy(c->elemsize, ep, nil);
	if(received != nil)
		*received = false;
	if(raceenabled)
		runtime·raceacquire(c);
	runtime·unlock(c);
	if(mysg.releasetime > 0)
		runtime·blockevent(mysg.releasetime - t0, 2);
	return true;
}
```

(3) 对于同步channel，如果有发送者，则从拷贝数据，并唤醒发送者；否则，将自己加到等待队列recvq，并陷入阻塞:

```c
	sg = dequeue(&c->sendq);
	if(sg != nil) {
		if(raceenabled)
			racesync(c, sg);
		runtime·unlock(c);

		if(ep != nil)
			c->elemtype->alg->copy(c->elemsize, ep, sg->elem);
		gp = sg->g;
		gp->param = sg;
		if(sg->releasetime)
			sg->releasetime = runtime·cputicks();
		runtime·ready(gp); ///唤醒发送者

		if(received != nil)
			*received = true;
		return true;
	}


	mysg.elem = ep;
	mysg.g = g;
	mysg.selectdone = nil;
	g->param = nil;
	enqueue(&c->recvq, &mysg);
	runtime·parkunlock(c, "chan receive"); ///陷入阻塞
```

(4) 对于异步接收，如果缓冲区有数据，则拷贝数据，并唤醒发送者(如果sendq不为空)；否则，将自己加到等待队列recvq，并陷入阻塞。

## Close

close channel时，会唤醒所有等待的sender和receiver:

```c
func closechan(c *Hchan) {
	closechan(c, runtime·getcallerpc(&c));
}

static void
closechan(Hchan *c, void *pc)
{
	SudoG *sg;
	G* gp;
...
	runtime·lock(c);

...
	c->closed = true; /// 设置close标志

	// release all readers
	for(;;) {
		sg = dequeue(&c->recvq);
		if(sg == nil)
			break;
		gp = sg->g;
		gp->param = nil;
		if(sg->releasetime)
			sg->releasetime = runtime·cputicks();
		runtime·ready(gp);
	}

	// release all writers
	for(;;) {
		sg = dequeue(&c->sendq);
		if(sg == nil)
			break;
		gp = sg->g;
		gp->param = nil;
		if(sg->releasetime)
			sg->releasetime = runtime·cputicks();
		runtime·ready(gp);
	}

	runtime·unlock(c);
}
```

## 总结

对于Send/Receive/Close所有的操作，都是在Hchan.Lock的保护下进行的。所以，对channel的读写是线程安全的。