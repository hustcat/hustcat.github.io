---
layout: post
title: Dive into memory management in Go
date: 2016-08-16 17:00:30
categories: 编程语言
tags: golang
excerpt: Dive into memory management in Go
---

Go的内存管理是基于[tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html)实现的。主要解决小对象的分配和多线程问题。小于等于32K的为小对象，分100种大小，每种大小一个链表。


## 1 数据结构

有一些基本的数据结构：

> FixAlloc: a free-list allocator for fixed-size objects,
		used to manage storage used by the allocator.
>
> MHeap: the malloc heap, managed at page (4096-byte) granularity.
>
> MSpan: a run of pages managed by the MHeap.
>
> MCentral: a shared free list for a given size class.
>
> MCache: a per-thread (in Go, per-P) cache for small objects.
>
> MStats: allocation statistics.

### 1.1 MSpan

每个`MSpan`代表一些连续的page。

```c
struct MSpan
{
	MSpan	*next;		// in a span linked list
	MSpan	*prev;		// in a span linked list
	PageID	start;		// starting page number
	uintptr	npages;		// number of pages in span
	MLink	*freelist;	// list of free objects
...
}
```

### 1.2 MHeap

```c

MaxMHeapList = 1<<(20 - PageShift),	// Maximum page length for fixed-size list in MHeap.
HeapAllocChunk = 1<<20,		// Chunk size for heap growth

// Main malloc heap.
// The heap itself is the "free[]" and "large" arrays,
// but all the other global data is here too.
struct MHeap
{
	Lock;
	MSpan free[MaxMHeapList];	// free lists of given length, 256
	MSpan freelarge;		// free lists length >= MaxMHeapList
	MSpan busy[MaxMHeapList];	// busy lists of large objects of given length
	MSpan busylarge;		// busy lists of large objects length >= MaxMHeapList
	MSpan **allspans;		// all spans out there
	MSpan **sweepspans;		// copy of allspans referenced by sweeper
	uint32	nspan;
	uint32	nspancap;
	uint32	sweepgen;		// sweep generation, see comment in MSpan
	uint32	sweepdone;		// all spans are swept

	// span lookup
	MSpan**	spans; ///MSpan指针数组,每个Pageg一项
	uintptr	spans_mapped;

	// range of addresses we might see in the heap
	byte *bitmap;
	uintptr bitmap_mapped;
	byte *arena_start;
	byte *arena_used;
	byte *arena_end;
	bool arena_reserved;

	// central free lists for small size classes.
	// the padding makes sure that the MCentrals are
	// spaced CacheLineSize bytes apart, so that each MCentral.Lock
	// gets its own cache line.
	struct {
		MCentral;
		byte pad[CacheLineSize];
	} central[NumSizeClasses];

	FixAlloc spanalloc;	// allocator for Span*
	FixAlloc cachealloc;	// allocator for MCache*
	FixAlloc specialfinalizeralloc;	// allocator for SpecialFinalizer*
	FixAlloc specialprofilealloc;	// allocator for SpecialProfile*
	Lock speciallock; // lock for sepcial record allocators.

	// Malloc stats.
	uint64 largefree;	// bytes freed for large objects (>MaxSmallSize)
	uint64 nlargefree;	// number of frees for large objects (>MaxSmallSize)
	uint64 nsmallfree[NumSizeClasses];	// number of frees for small objects (<=MaxSmallSize)
};
extern MHeap runtime·mheap; ///全局heap对象
```

MHeap负责维护管理的MSpan:

* (1)MHeap->spans

MSpan指针数组，每个page对应一项，相同MSpan的所有page指向同一个MSpan对象。

page -> MSpan:

```c
// Try to add at least npage pages of memory to the heap,
// returning whether it worked.
static bool
MHeap_Grow(MHeap *h, uintptr npage)
{
	// Create a fake "in use" span and free it, so that the
	// right coalescing happens.
	s = runtime·FixAlloc_Alloc(&h->spanalloc); ///分配MSpan对象
	runtime·MSpan_Init(s, (uintptr)v>>PageShift, ask>>PageShift);
	p = s->start;
	p -= ((uintptr)h->arena_start>>PageShift); ///address -> MSpan offset
	h->spans[p] = s;
	h->spans[p + s->npages - 1] = s;
```

* (2)MHeap->free

`MHeap->free`共有256项，每项对应一个MSpan链表，每个链表中的MSpan有相同数量(链表下标)的page。

### 1.3 MCentral

MHeap有一个全局共享的MCentral。

```c
// Central list of free objects of a given size.
struct MCentral
{
	Lock;
	int32 sizeclass;
	MSpan nonempty;	// list of spans with a free object
	MSpan empty;	// list of spans with no free objects (or cached in an MCache)
	int32 nfree;	// # of objects available in nonempty spans
};
```

### 1.4 MCache

每个M都有一个自己的局部内存缓存MCache，这样分配小对象(小于32KB)的时候直接从MCache中分配，就不用加锁了，这是Go能够在多线程环境中高效地进行内存分配的重要原因。MCache是用于小对象的分配。

```
enum
{
	// Computed constant.  The definition of MaxSmallSize and the
	// algorithm in msize.c produce some number of different allocation
	// size classes.  NumSizeClasses is that number.  It's needed here
	// because there are static arrays of this length; when msize runs its
	// size choosing algorithm it double-checks that NumSizeClasses agrees.
	NumSizeClasses = 67,
}

// Per-thread (in Go, per-P) cache for small objects.
// No locking needed because it is per-thread (per-P).
struct MCache
{
	// The following members are accessed on every malloc,
	// so they are grouped here for better caching.
	int32 next_sample;		// trigger heap sample after allocating this many bytes
	intptr local_cachealloc;	// bytes allocated (or freed) from cache since last lock of heap
	// Allocator cache for tiny objects w/o pointers.
	// See "Tiny allocator" comment in malloc.goc.
	byte*	tiny;
	uintptr	tinysize;
	// The rest is not accessed on every malloc.
	MSpan*	alloc[NumSizeClasses];	// spans to allocate from
	MCacheList free[NumSizeClasses];// lists of explicitly freed objects
	// Local allocator stats, flushed during GC.
	uintptr local_nlookup;		// number of pointer lookups
	uintptr local_largefree;	// bytes freed for large objects (>MaxSmallSize)
	uintptr local_nlargefree;	// number of frees for large objects (>MaxSmallSize)
	uintptr local_nsmallfree[NumSizeClasses];	// number of frees for small objects (<=MaxSmallSize)
};
```

`MCache->alloc`用于内存分配的`MSpan`链表数组，每种大小一个项(链表)。

## 2 初始化

`runtime·mallocinit`完成初始化，它会从OS预留一段内存(arena_size+bitmap_size+spans_size)。其中`arena`用于分配的内存；`bitmap`是位图，每个word对应4bit；`spans`是MSpan指针数组。

整体布局如下：

```
                                             arena_start                            arena_end
+---------------+----------------------------+--------------------------------------+
|  spans        |  bitmap                    |  arena (128G)                        |
+---------------+----------------------------+--------------------------------------+
spans_mapped ->              <- bitmap_mapped arena_used ->
```

注意，这里只是预留，并没有分配。

```
//runtime/malloc.goc
void
runtime·mallocinit(void)
{
///...
	// Set up the allocation arena, a contiguous area of memory where
	// allocated data will be found.  The arena begins with a bitmap large
	// enough to hold 4 bits per allocated word.
	if(sizeof(void*) == 8 && (limit == 0 || limit > (1<<30))) {
		// On a 64-bit machine, allocate from a single contiguous reservation.
		// 128 GB (MaxMem) should be big enough for now.
		arena_size = MaxMem; //128GB
		bitmap_size = arena_size / (sizeof(void*)*8/4); ///4 bits per allocated word
		spans_size = arena_size / PageSize * sizeof(runtime·mheap.spans[0]); ///每个page一个MSpan指针项
		spans_size = ROUND(spans_size, PageSize);
		for(i = 0; i <= 0x7f; i++) {
			p = (void*)(i<<40 | 0x00c0ULL<<32);
			p_size = bitmap_size + spans_size + arena_size + PageSize;
			p = runtime·SysReserve(p, p_size, &reserved); ///预留一个以0x0000XXc000000000(XX=00...7f)开始的一段内存
			if(p != nil)
				break;
		}
	}

	// PageSize can be larger than OS definition of page size,
	// so SysReserve can give us a PageSize-unaligned pointer.
	// To overcome this we ask for PageSize more and round up the pointer.
	p1 = (byte*)ROUND((uintptr)p, PageSize);

	runtime·mheap.spans = (MSpan**)p1;
	runtime·mheap.bitmap = p1 + spans_size;
	runtime·mheap.arena_start = p1 + spans_size + bitmap_size;
	runtime·mheap.arena_used = runtime·mheap.arena_start;
	runtime·mheap.arena_end = p + p_size;
	runtime·mheap.arena_reserved = reserved;


	// Initialize the rest of the allocator.	
	runtime·MHeap_Init(&runtime·mheap);
	m->mcache = runtime·allocmcache();

	// See if it works.
	runtime·free(runtime·malloc(TinySize));
}
```

当MHeap需要扩展时(MHeap_Grow)，从OS请求内存时，会更新`MHeap->arena_used`、`MHeap->spans_mapped`和`MHeap->spans_mapped`几个字段:

```c
///runtime/malloc.goc
void*
runtime·MHeap_SysAlloc(MHeap *h, uintptr n)
{
///...
	if(n <= h->arena_end - h->arena_used) { ///area空间足够
		// Keep taking from our reservation.
		p = h->arena_used;
		runtime·SysMap(p, n, h->arena_reserved, &mstats.heap_sys);
		h->arena_used += n;
		runtime·MHeap_MapBits(h); ///更新bitmap_mapped
		runtime·MHeap_MapSpans(h); ///更新spans_mapped
		if(raceenabled)
			runtime·racemapshadow(p, n);
		
		if(((uintptr)p & (PageSize-1)) != 0)
			runtime·throw("misrounded allocation in MHeap_SysAlloc");
		return p;
	}
```

bitmap主要用于GC，后面再讨论。

* MHeap初始化

```
// runtime/mheap.c
// Initialize the heap; fetch memory using alloc.
void
runtime·MHeap_Init(MHeap *h)
{
	uint32 i;

	runtime·FixAlloc_Init(&h->spanalloc, sizeof(MSpan), RecordSpan, h, &mstats.mspan_sys);
	runtime·FixAlloc_Init(&h->cachealloc, sizeof(MCache), nil, nil, &mstats.mcache_sys);
	runtime·FixAlloc_Init(&h->specialfinalizeralloc, sizeof(SpecialFinalizer), nil, nil, &mstats.other_sys);
	runtime·FixAlloc_Init(&h->specialprofilealloc, sizeof(SpecialProfile), nil, nil, &mstats.other_sys);
	// h->mapcache needs no init
	for(i=0; i<nelem(h->free); i++) {
		runtime·MSpanList_Init(&h->free[i]);
		runtime·MSpanList_Init(&h->busy[i]);
	}
	runtime·MSpanList_Init(&h->freelarge);
	runtime·MSpanList_Init(&h->busylarge);
	for(i=0; i<nelem(h->central); i++)
		runtime·MCentral_Init(&h->central[i], i);
}
```

## 3 内存分配

```
// Runtime stubs.

void*
runtime·mal(uintptr n)
{
	return runtime·mallocgc(n, 0, 0);
}

#pragma textflag NOSPLIT
func new(typ *Type) (ret *uint8) {
	ret = runtime·mallocgc(typ->size, (uintptr)typ | TypeInfo_SingleObject, typ->kind&KindNoPointers ? FlagNoScan : 0);
}
```


```
// Allocate an object of at least size bytes.
// Small objects are allocated from the per-thread cache's free lists.
// Large objects (> 32 kB) are allocated straight from the heap.
// If the block will be freed with runtime·free(), typ must be 0.
void*
runtime·mallocgc(uintptr size, uintptr typ, uint32 flag)
{
//...
	c = m->mcache;
	if(!runtime·debug.efence && size <= MaxSmallSize) {/// <=32KB
		if((flag&(FlagNoScan|FlagNoGC)) == FlagNoScan && size < TinySize) {
			// Tiny allocator. Tiny对象分配(小于16字节,没有指针)
			tinysize = c->tinysize;
			if(size <= tinysize) {
				tiny = c->tiny;
				// Align tiny pointer for required (conservative) alignment.
				if((size&7) == 0)
					tiny = (byte*)ROUND((uintptr)tiny, 8);
				else if((size&3) == 0)
					tiny = (byte*)ROUND((uintptr)tiny, 4);
				else if((size&1) == 0)
					tiny = (byte*)ROUND((uintptr)tiny, 2);
				size1 = size + (tiny - c->tiny);
				if(size1 <= tinysize) {
					// The object fits into existing tiny block.
					v = (MLink*)tiny;
					c->tiny += size1;
					c->tinysize -= size1;
					m->mallocing = 0;
					m->locks--;
					if(m->locks == 0 && g->preempt)  // restore the preemption request in case we've cleared it in newstack
						g->stackguard0 = StackPreempt;
					return v;
				}
			}
			// Allocate a new TinySize block.
			s = c->alloc[TinySizeClass];
			if(s->freelist == nil)
				s = runtime·MCache_Refill(c, TinySizeClass);
			v = s->freelist;
			next = v->next;
			s->freelist = next;
			s->ref++;
			if(next != nil)  // prefetching nil leads to a DTLB miss
				PREFETCH(next);
			((uint64*)v)[0] = 0;
			((uint64*)v)[1] = 0;
			// See if we need to replace the existing tiny block with the new one
			// based on amount of remaining free space.
			if(TinySize-size > tinysize) {
				c->tiny = (byte*)v + size;
				c->tinysize = TinySize - size;
			}
			size = TinySize;
			goto done;
		}
		// Allocate from mcache free lists.
		// Inlined version of SizeToClass().
		if(size <= 1024-8)
			sizeclass = runtime·size_to_class8[(size+7)>>3];
		else
			sizeclass = runtime·size_to_class128[(size-1024+127) >> 7];
		size = runtime·class_to_size[sizeclass];
		s = c->alloc[sizeclass];
		if(s->freelist == nil)
			s = runtime·MCache_Refill(c, sizeclass);
		v = s->freelist;
		next = v->next;
		s->freelist = next;
		s->ref++;
		if(next != nil)  // prefetching nil leads to a DTLB miss
			PREFETCH(next);
		if(!(flag & FlagNoZero)) {
			v->next = nil;
			// block is zeroed iff second word is zero ...
			if(size > 2*sizeof(uintptr) && ((uintptr*)v)[1] != 0)
				runtime·memclr((byte*)v, size);
		}
	done:
		c->local_cachealloc += size;
	} else { /// >32KB
		// Allocate directly from heap.
		s = largealloc(flag, &size);
		v = (void*)(s->start << PageShift);
	}

	return v;
}
```

从上面代码可以看到，对于小对象(小于32KB)，从MCache分配；对于大对象，则直接从Heap分配。

### 3.1 Alloc from heap

对于大对象(>=32KB)，直接从Heap分配一个MSpan，并返回：

```
static MSpan*
largealloc(uint32 flag, uintptr *sizep)
{
	uintptr npages, size;
	MSpan *s;
	void *v;

	// Allocate directly from heap.
	size = *sizep;
	if(size + PageSize < size)
		runtime·throw("out of memory");
	npages = size >> PageShift;
	if((size & PageMask) != 0)
		npages++;
	s = runtime·MHeap_Alloc(&runtime·mheap, npages, 0, 1, !(flag & FlagNoZero));
	if(s == nil)
		runtime·throw("out of memory");
	s->limit = (byte*)(s->start<<PageShift) + size;
	*sizep = npages<<PageShift;
	v = (void*)(s->start << PageShift);
	// setup for mark sweep
	runtime·markspan(v, 0, 0, true);
	return s;
}


// Allocate a new span of npage pages from the heap
// and record its size class in the HeapMap and HeapMapCache.
MSpan*
runtime·MHeap_Alloc(MHeap *h, uintptr npage, int32 sizeclass, bool large, bool needzero)
{
	MSpan *s;

	runtime·lock(h);
	mstats.heap_alloc += m->mcache->local_cachealloc;
	m->mcache->local_cachealloc = 0;
	s = MHeap_AllocLocked(h, npage, sizeclass); ///得到MSpan
	if(s != nil) {
		mstats.heap_inuse += npage<<PageShift;
		if(large) {///对于大对象,将MSpan加到busy链表
			mstats.heap_objects++;
			mstats.heap_alloc += npage<<PageShift;
			// Swept spans are at the end of lists.
			if(s->npages < nelem(h->free))
				runtime·MSpanList_InsertBack(&h->busy[s->npages], s);
			else
				runtime·MSpanList_InsertBack(&h->busylarge, s);
		}
	}
	runtime·unlock(h);

	return s;
}
```

`MHeap_AllocLocked`是MHeap分配MSpan的核心函数，在对MHeap加锁的情况下调用:

> (1)先尝试从`h->free`中的链表分配MSpan；
>
> (2)如果失败，则尝试从`h->freelarge`分配MSpan；
>
> (3)还是失败，则先扩展MHeap，再从`h->freelarge`分配MSpan。

```c
static MSpan*
MHeap_AllocLocked(MHeap *h, uintptr npage, int32 sizeclass)
{
	uintptr n;
	MSpan *s, *t;
	PageID p;

	// To prevent excessive heap growth, before allocating n pages
	// we need to sweep and reclaim at least n pages.
	if(!h->sweepdone)
		MHeap_Reclaim(h, npage);
	///找到合适大小的链表
	// Try in fixed-size lists up to max.
	for(n=npage; n < nelem(h->free); n++) {
		if(!runtime·MSpanList_IsEmpty(&h->free[n])) {
			s = h->free[n].next;
			goto HaveSpan;
		}
	}

	// Best fit in list of large spans. >=1MB的对象
	if((s = MHeap_AllocLarge(h, npage)) == nil) {
		if(!MHeap_Grow(h, npage)) ///分配失败,则扩展heap
			return nil;
		if((s = MHeap_AllocLarge(h, npage)) == nil)
			return nil;
	}

HaveSpan:
	// Mark span in use.
	if(s->state != MSpanFree)
		runtime·throw("MHeap_AllocLocked - MSpan not free");
	if(s->npages < npage)
		runtime·throw("MHeap_AllocLocked - bad npages");
	runtime·MSpanList_Remove(s); ///从链表中移除s
	runtime·atomicstore(&s->sweepgen, h->sweepgen);
	s->state = MSpanInUse; ///MSpanInUse
	mstats.heap_idle -= s->npages<<PageShift;
	mstats.heap_released -= s->npreleased<<PageShift;
	if(s->npreleased > 0)
		runtime·SysUsed((void*)(s->start<<PageShift), s->npages<<PageShift);
	s->npreleased = 0;

	if(s->npages > npage) {/// s分成2个MSpan
		// Trim extra and put it back in the heap.
		t = runtime·FixAlloc_Alloc(&h->spanalloc);///分配一个新的MSpan
		runtime·MSpan_Init(t, s->start + npage, s->npages - npage);
		s->npages = npage;
		p = t->start;
		p -= ((uintptr)h->arena_start>>PageShift);
		if(p > 0)
			h->spans[p-1] = s;
		h->spans[p] = t;
		h->spans[p+t->npages-1] = t;
		t->needzero = s->needzero;
		runtime·atomicstore(&t->sweepgen, h->sweepgen);
		t->state = MSpanInUse;
		MHeap_FreeLocked(h, t); ///将剩余的MSpan放回空闲链
		t->unusedsince = s->unusedsince; // preserve age
	}
	s->unusedsince = 0;

	// Record span info, because gc needs to be
	// able to map interior pointer to containing span.
	s->sizeclass = sizeclass;
	s->elemsize = (sizeclass==0 ? s->npages<<PageShift : runtime·class_to_size[sizeclass]);
	s->types.compression = MTypes_Empty;
	p = s->start;
	p -= ((uintptr)h->arena_start>>PageShift);
	for(n=0; n<npage; n++)
		h->spans[p+n] = s; ///设置h->spans,指向相同的MSpan
	return s;
}
```

* 扩展heap

如果heap的所有空闲链表(free和freelarge)都无法内存分配请求时，会扩展heap。每次扩展16个page的整数倍，而且至少1MB。所以，扩展后新创建的MSapn必然加入`h->freelarge`链表。

```c
/// runtime/mheap.c
// Try to add at least npage pages of memory to the heap,
// returning whether it worked.
static bool
MHeap_Grow(MHeap *h, uintptr npage)
{
	uintptr ask;
	void *v;
	MSpan *s;
	PageID p;

	// Ask for a big chunk, to reduce the number of mappings
	// the operating system needs to track; also amortizes
	// the overhead of an operating system mapping.
	// Allocate a multiple of 64kB (16 pages).
	npage = (npage+15)&~15; //16个page的整数倍
	ask = npage<<PageShift;
	if(ask < HeapAllocChunk) ///至少1MB
		ask = HeapAllocChunk;

	v = runtime·MHeap_SysAlloc(h, ask);///从OS申请内存
	if(v == nil) {
		if(ask > (npage<<PageShift)) {
			ask = npage<<PageShift;
			v = runtime·MHeap_SysAlloc(h, ask);
		}
		if(v == nil) {
			runtime·printf("runtime: out of memory: cannot allocate %D-byte block (%D in use)\n", (uint64)ask, mstats.heap_sys);
			return false;
		}
	}

	// Create a fake "in use" span and free it, so that the
	// right coalescing happens.
	s = runtime·FixAlloc_Alloc(&h->spanalloc); ///分配MSpan对象
	runtime·MSpan_Init(s, (uintptr)v>>PageShift, ask>>PageShift);
	p = s->start;
	p -= ((uintptr)h->arena_start>>PageShift); ///address -> MSpan offset
	h->spans[p] = s;
	h->spans[p + s->npages - 1] = s;
	runtime·atomicstore(&s->sweepgen, h->sweepgen);
	s->state = MSpanInUse;
	MHeap_FreeLocked(h, s); ///加入空闲链表(freelarge)
	return true;
}
```

### 3.2 Alloc from MCache

分配一个小对象(<32kB)的过程：

(1)将小对象大小向上取整到一个对应的尺寸类别，查找相应的MCache的空闲链表。如果链表不空，直接从上面分配一个对象。这个过程可以不必加锁。

(2)如果MCache空闲链是空的,通过从MCentral空闲链拿一些对象进行补充。

```c
void*
runtime·mallocgc(uintptr size, uintptr typ, uint32 flag)
{
//...
		if(s->freelist == nil)
			s = runtime·MCache_Refill(c, sizeclass);
}

// Gets a span that has a free object in it and assigns it
// to be the cached span for the given sizeclass.  Returns this span.
MSpan*
runtime·MCache_Refill(MCache *c, int32 sizeclass)
{

	// Get a new cached span from the central lists.
	s = runtime·MCentral_CacheSpan(&runtime·mheap.central[sizeclass]);

	c->alloc[sizeclass] = s;
	m->locks--;
	return s;
}
```

(3)如果MCentral空闲链是空的,则通过MHeap取一些MSpan，加到MCentral的空闲链表:

```c
//runtime/mcentral.c
// Allocate a span to use in an MCache.
MSpan*
runtime·MCentral_CacheSpan(MCentral *c)
{
///...
retry:
	for(s = c->nonempty.next; s != &c->nonempty; s = s->next) { ///有空闲空间MSpan链表
		// we have a nonempty span that does not require sweeping, allocate from it
		goto havespan;
	}

	// Replenish central list if empty.
	if(!MCentral_Grow(c)) {///从heap取一个MSpan
		runtime·unlock(c);
		return nil;
	}
	goto retry;
}

havespan:
	cap = (s->npages << PageShift) / s->elemsize;
	n = cap - s->ref;

	c->nfree -= n;
	runtime·MSpanList_Remove(s);
	runtime·MSpanList_InsertBack(&c->empty, s); ///加到非空闲链表
	s->incache = true;
	runtime·unlock(c);
	return s;
}
```

(4)如果MHeap是空的,或者没有足够大小的页了,从操作系统分配一组新的页(至少1MB)。


### 3.3 FixAlloc
`runtime`有一些固定大小的对象，比如`MSpan`，这些对象由`FixAlloc`负责分配。

```c
// FixAlloc is a simple free-list allocator for fixed size objects.
// Malloc uses a FixAlloc wrapped around SysAlloc to manages its
// MCache and MSpan objects.
//
// Memory returned by FixAlloc_Alloc is not zeroed.
// The caller is responsible for locking around FixAlloc calls.
// Callers can keep state in the object but the first word is
// smashed by freeing and reallocating.
struct FixAlloc
{
	uintptr	size; ///entry size
	void	(*first)(void *arg, byte *p);	// called first time p is returned
	void*	arg;
	MLink*	list;
	byte*	chunk; ///内存(还有空闲空间)
	uint32	nchunk; ///chunk所指内存大小
	uintptr	inuse;	// in-use bytes now
	uint64*	stat;
};

// A generic linked list of blocks.  (Typically the block is bigger than sizeof(MLink).)
struct MLink
{
	MLink *next;
};
```

```c
///runtime/mfixalloc.c
void*
runtime·FixAlloc_Alloc(FixAlloc *f)
{
	void *v;
	
	if(f->list) {///链表有释放的entry(entry释放时,会加入该list)
		v = f->list;
		f->list = *(void**)f->list;///指向下一个entry
		f->inuse += f->size;
		return v;
	}
	if(f->nchunk < f->size) { ///内存不够,则分配一块新的内存
		f->chunk = runtime·persistentalloc(FixAllocChunk, 0, f->stat); ///从系统分配FixAllocChunk大小的内存
		f->nchunk = FixAllocChunk;///16KB
	}
	v = f->chunk;
	if(f->first)
		f->first(f->arg, v);
	f->chunk += f->size;
	f->nchunk -= f->size;
	f->inuse += f->size;
	return v;
}
```

## 内存释放

### runtime·free

`runtime·free`是释放对象的入口函数:

```c
// Free the object whose base pointer is v.
void
runtime·free(void *v)
{

```

address -> MSpan:

```c
int32
runtime·mlookup(void *v, byte **base, uintptr *size, MSpan **sp)
{
	s = runtime·MHeap_LookupMaybe(&runtime·mheap, v);
///...
}

MSpan*
runtime·MHeap_LookupMaybe(MHeap *h, void *v)
{
	MSpan *s;
	PageID p, q;

	p = (uintptr)v>>PageShift;
	q = p;
	q -= (uintptr)h->arena_start >> PageShift; ///offset from arena_start
	s = h->spans[q];
	return s;
}
```

* zero

当MSpan返回MHeap的时候，会设置needzero:

```c
// Free the object whose base pointer is v.
void
runtime·free(void *v)
{
///...
    if(sizeclass == 0) {
        // Large object.
        s->needzero = 1; ///大对象直接返给heap
            runtime·MHeap_Free(&runtime·mheap, s, 1);
        c->local_nlargefree++;
        c->local_largefree += size;
    }
```

MSpan从MCentral还回MHeap:

```c
// Return s to the heap.  s must be unused (s->ref == 0).  Unlocks c.
static void
MCentral_ReturnToHeap(MCentral *c, MSpan *s)
{
    s->needzero = 1
```


### Free to MHeap

```c
// Free the span back into the heap.
void
runtime·MHeap_Free(MHeap *h, MSpan *s, int32 acct)
{
	runtime·lock(h);
	mstats.heap_alloc += m->mcache->local_cachealloc;
	m->mcache->local_cachealloc = 0;
	mstats.heap_inuse -= s->npages<<PageShift;
	if(acct) {
		mstats.heap_alloc -= s->npages<<PageShift;
		mstats.heap_objects--;
	}
	MHeap_FreeLocked(h, s);
	runtime·unlock(h);
}
///已经调用runtime.lock(h)
static void
MHeap_FreeLocked(MHeap *h, MSpan *s)
{
	MSpan *t;
	PageID p;

	s->types.compression = MTypes_Empty;

	if(s->state != MSpanInUse || s->ref != 0 || s->sweepgen != h->sweepgen) {
		runtime·printf("MHeap_FreeLocked - span %p ptr %p state %d ref %d sweepgen %d/%d\n",
			s, s->start<<PageShift, s->state, s->ref, s->sweepgen, h->sweepgen);
		runtime·throw("MHeap_FreeLocked - invalid free");
	}
	mstats.heap_idle += s->npages<<PageShift;
	s->state = MSpanFree;  ///MSpanFree状态
	runtime·MSpanList_Remove(s); ///从链表删除
	// Stamp newly unused spans. The scavenger will use that
	// info to potentially give back some pages to the OS.
	s->unusedsince = runtime·nanotime(); ///更新unused time
	s->npreleased = 0;
	///尝试与前后MSpan合并
	// Coalesce with earlier, later spans.
	p = s->start;
	p -= (uintptr)h->arena_start >> PageShift;
	if(p > 0 && (t = h->spans[p-1]) != nil && t->state != MSpanInUse) {
		s->start = t->start;
		s->npages += t->npages;
		s->npreleased = t->npreleased; // absorb released pages
		s->needzero |= t->needzero;
		p -= t->npages;
		h->spans[p] = s;
		runtime·MSpanList_Remove(t);
		t->state = MSpanDead;
		runtime·FixAlloc_Free(&h->spanalloc, t);
	}
	if((p+s->npages)*sizeof(h->spans[0]) < h->spans_mapped && (t = h->spans[p+s->npages]) != nil && t->state != MSpanInUse) {
		s->npages += t->npages;
		s->npreleased += t->npreleased;
		s->needzero |= t->needzero;
		h->spans[p + s->npages - 1] = s;
		runtime·MSpanList_Remove(t);
		t->state = MSpanDead;
		runtime·FixAlloc_Free(&h->spanalloc, t);
	}

	// Insert s into appropriate list.
	if(s->npages < nelem(h->free))
		runtime·MSpanList_Insert(&h->free[s->npages], s);
	else ///大对象MSpan,放到large list
		runtime·MSpanList_Insert(&h->freelarge, s);
}
```


## Reference

* [TCMalloc : Thread-Caching Malloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html)
* [Go GC: Prioritizing low latency and simplicity](https://blog.golang.org/go15gc)
* [Go语言内存分配器设计](http://skoo.me/go/2013/10/08/go-memory-manage-system-design/)
* [6.1 内存池](https://tiancaiamao.gitbooks.io/go-internals/content/zh/06.2.html)
