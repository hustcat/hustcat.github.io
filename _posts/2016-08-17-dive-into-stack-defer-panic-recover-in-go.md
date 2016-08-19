---
layout: post
title: Dive into stack and defer/panic/recover in go
date: 2016-08-17 15:00:30
categories: 编程语言
tags: golang
excerpt: Dive into stack and defer/panic/recover in go
---

## Contiguous stacks

在C语言中，每个线程的栈的大小是固定不变的。Go语言中的每个goroutine都有一个栈。如果栈的大小固定，太小容易导致溢出，太大又容易浪费空间(goroutine的数量可能很多)。为此，Go引入了按需分配策略，在开始时分配很小的空间(8192字节)，并且支持按需增长。Go1.3之后的版本都使用新的[Contiguous stacks](https://docs.google.com/document/d/1wAaf1rYoM4S4gtnPh0zOlGzWtrZFQ5suE8qr2sD8uWQ/pub)。[这篇文章](https://blog.cloudflare.com/how-stacks-are-handled-in-go/)详细讨论了为什么使用`Contiguous stacks`。

Go编译器会在每个函数的开始，插入一段检测栈是否够用的代码。如果空间不够，则会转到`runtime.morestack_noctxt`，分配一个新的足够大的栈空间，将旧栈的内容拷贝到新栈中，然后再执行函数。

```c
func f1(){
    fmt.Println("hello")
}
func main(){
    f1()
}
```

```
(gdb) disass 
Dump of assembler code for function main.main:
   0x0000000000002140 <+0>:	mov    %gs:0x8a0,%rcx
=> 0x0000000000002149 <+9>:	cmp    0x10(%rcx),%rsp
   0x000000000000214d <+13>:	jbe    0x2155 <main.main+21> //如果rsp <= 0x10(%rcx),则调用runtime.morestack_noctxt
   0x000000000000214f <+15>:	callq  0x2040 <main.f1>
   0x0000000000002154 <+20>:	retq   
   0x0000000000002155 <+21>:	callq  0x52d10 <runtime.morestack_noctxt>
   0x000000000000215a <+26>:	jmp    0x2140 <main.main>
   0x000000000000215c <+28>:	add    %al,(%rax)
   0x000000000000215e <+30>:	add    %al,(%rax)

(gdb) disass
Dump of assembler code for function main.f1:
=> 0x0000000000002040 <+0>:	mov    %gs:0x8a0,%rcx
   0x0000000000002049 <+9>:	cmp    0x10(%rcx),%rsp
   0x000000000000204d <+13>:	jbe    0x212c <main.f1+236>
...
   0x000000000000212c <+236>:	callq  0x52d10 <runtime.morestack_noctxt>
   0x0000000000002131 <+241>:	jmpq   0x2040 <main.f1>
```

## alloc stack

G与栈相关的几个字段:

```c
struct	G
{
	// stackguard0 can be set to StackPreempt as opposed to stackguard
	uintptr	stackguard0;	// cannot move - also known to linker, libmach, runtime/cgo
	uintptr	stackbase;	// cannot move - also known to libmach, runtime/cgo
	uintptr	stackguard;	// same as stackguard0, but not set to StackPreempt
	uintptr	stack0;
	uintptr	stacksize;
...
}
```

栈的结构:

```
stack0          stackguard (stackguard0)              stackbase 
+---------------+-------------------------------------+---------------+
| StackGuard    | STACK                               | Stktop        |
+---------------+-------------------------------------+---------------+ 
low          <--------------------------------------- SP              high
```

创建G的时候，默认分配8192字节的栈:

```c
// runtime/proc.c
G*
runtime·newproc1(FuncVal *fn, byte *argp, int32 narg, int32 nret, void *callerpc)
{
...
	if((newg = gfget(p)) != nil) {
		if(newg->stackguard - StackGuard != newg->stack0)
			runtime·throw("invalid stack in newg");
	} else {
		newg = runtime·malg(StackMin); //8192
		allgadd(newg);
	}
...
}

// Allocate a new g, with a stack big enough for stacksize bytes.
G*
runtime·malg(int32 stacksize)
{
	G *newg;
	byte *stk;
...
	newg = allocg();
	if(stacksize >= 0) {
		stacksize = runtime·round2(StackSystem + stacksize);
		if(g == m->g0) {
			// running on scheduler stack already.
			stk = runtime·stackalloc(newg, stacksize);
		} else {
			// have to call stackalloc on scheduler stack.
			newg->stacksize = stacksize;
			g->param = newg;
			runtime·mcall(mstackalloc);
			stk = g->param;
			g->param = nil;
		}
		newg->stack0 = (uintptr)stk;
		newg->stackguard = (uintptr)stk + StackGuard;
		newg->stackguard0 = newg->stackguard;
		newg->stackbase = (uintptr)stk + stacksize - sizeof(Stktop);
	}
	return newg;
}
```

`stack0`为栈的内存起始地址，`stackbase`为栈底，`stackguard`为top的上限。`StackGuard`和`Stktop`预留的空间。


## runtime·morestack

```
/* runtime/asm_amd64.s
 * support for morestack
 */

// Called during function prolog when more stack is needed.
// Caller has already done get_tls(CX); MOVQ m(CX), BX.
//
// The traceback routines see morestack on a g0 as being
// the top of a stack (for example, morestack calling newstack
// calling the scheduler calling newm calling gc), so we must
// record an argument size. For that purpose, it has no arguments.
TEXT runtime·morestack(SB),NOSPLIT,$0-0
	// Cannot grow scheduler stack (m->g0).
	MOVQ	m_g0(BX), SI
	CMPQ	g(CX), SI
	JNE	2(PC)
	INT	$3
...
	// Call newstack on m->g0's stack.
	MOVQ	m_g0(BX), BP
	MOVQ	BP, g(CX)
	MOVQ	(g_sched+gobuf_sp)(BP), SP
	CALL	runtime·newstack(SB)
	MOVQ	$0, 0x1003	// crash if newstack returns
	RET
```

`runtime·morestack`调用`runtime·newstack`完成栈的扩展:

```c
// Called from runtime·newstackcall or from runtime·morestack when a new
// stack segment is needed.  Allocate a new stack big enough for
// m->moreframesize bytes, copy m->moreargsize bytes to the new frame,
// and then act as though runtime·lessstack called the function at
// m->morepc.
void
runtime·newstack(void)
{
...
	// If every frame on the top segment is copyable, allocate a bigger segment
	// and move the segment instead of allocating a new segment.
	if(runtime·copystack) {
		if(!runtime·precisestack)
			runtime·throw("can't copy stacks without precise stacks");
		nframes = copyabletopsegment(gp);
		if(nframes != -1) {
			oldstk = (byte*)gp->stackguard - StackGuard;
			oldbase = (byte*)gp->stackbase + sizeof(Stktop);
			oldsize = oldbase - oldstk;
			newsize = oldsize * 2; ///增加为原来的2倍大小
			copystack(gp, nframes, newsize); ///拷贝栈
			if(StackDebug >= 1)
				runtime·printf("stack grow done\n");
			if(gp->stacksize > runtime·maxstacksize) {
				runtime·printf("runtime: goroutine stack exceeds %D-byte limit\n", (uint64)runtime·maxstacksize);
				runtime·throw("stack overflow");
			}
			gp->status = oldstatus;
			runtime·gogo(&gp->sched); ///重新执行G
		}
		// TODO: if stack is uncopyable because we're in C code, patch return value at
		// end of C code to trigger a copy as soon as C code exits.  That way, we'll
		// have stack available if we get this deep again.
	}
...
}
```

## defer

考虑如下示例:

```go
package main
import "sync"
var lock sync.Mutex
func test() {
    lock.Lock()
    defer lock.Unlock()
}
func main() {
    test()
}
```

```
(gdb) disass
Dump of assembler code for function main.test:
   0x0000000000002040 <+0>:	mov    %gs:0x8a0,%rcx
   0x0000000000002049 <+9>:	cmp    0x10(%rcx),%rsp
   0x000000000000204d <+13>:	jbe    0x20a2 <main.test+98>
   0x000000000000204f <+15>:	sub    $0x18,%rsp
=> 0x0000000000002053 <+19>:	lea    0xd3266(%rip),%rbx        # 0xd52c0 <main.lock>
   0x000000000000205a <+26>:	mov    %rbx,(%rsp)
   0x000000000000205e <+30>:	callq  0x4e640 <sync.(*Mutex).Lock>
   0x0000000000002063 <+35>:	lea    0xd3256(%rip),%rbx        # 0xd52c0 <main.lock>
   0x000000000000206a <+42>:	mov    %rbx,0x10(%rsp)
   0x000000000000206f <+47>:	movl   $0x8,(%rsp) # 参数size入栈
   0x0000000000002076 <+54>:	lea    0x86403(%rip),%rax        # 0x88480 <sync.(*Mutex).Unlock.f>
   0x000000000000207d <+61>:	mov    %rax,0x8(%rsp) ## Unlock入栈
   0x0000000000002082 <+66>:	callq  0x207f0 <runtime.deferproc>
   0x0000000000002087 <+71>:	cmp    $0x0,%eax
   0x000000000000208a <+74>:	jne    0x2097 <main.test+87>
   0x000000000000208c <+76>:	nop
   0x000000000000208d <+77>:	callq  0x21970 <runtime.deferreturn>
   0x0000000000002092 <+82>:	add    $0x18,%rsp
   0x0000000000002096 <+86>:	retq   
```

从汇编代码可以看到，main.test在返回之前，调用了`runtime.deferproc`和`runtime.deferreturn`，这是defer实现的核心。

* runtime·deferproc

`runtime·deferproc`会创建一个Defer对象，封装调用函数的信息，然后加到`G.defer`链表:

```c
/*
 * deferred subroutine calls
 */
struct Defer
{
	int32	siz;
	bool	special;	// not part of defer frame
	byte*	argp;		// where args were copied from
	byte*	pc;
	FuncVal*	fn;
	Defer*	link;
	void*	args[1];	// padded to actual size
};

// runtime/panic.c
uintptr
runtime·deferproc(int32 siz, FuncVal *fn, ...)
{
	Defer *d;
	// 创建Defer对象,加到G.defer链表
	d = newdefer(siz);
	d->fn = fn;
	d->pc = runtime·getcallerpc(&siz);

	/// fn的第一个参数地址，这个地址为调用者(即main.test)在调用deferproc之前的栈指针SP
	if(thechar == '5')
		d->argp = (byte*)(&fn+2);  // skip caller's saved link register
	else
		d->argp = (byte*)(&fn+1);
	runtime·memmove(d->args, d->argp, d->siz);

	// deferproc returns 0 normally.
	// a deferred func that stops a panic
	// makes the deferproc return 1.
	// the code the compiler generates always
	// checks the return value and jumps to the
	// end of the function if deferproc returns != 0.
	return 0;
}
```

可以看到，`runtime·deferproc`并没有调用函数。实际上，defer函数是在`runtime·deferreturn`中完成调用的，`runtime·deferreturn`会调用`G.defer`链表中的所有Defer对象封装的函数:

* runtime·deferproc

```c
// Run a deferred function if there is one.
// The compiler inserts a call to this at the end of any
// function which calls defer.
// If there is a deferred function, this will call runtime·jmpdefer,
// which will jump to the deferred function such that it appears
// to have been called by the caller of deferreturn at the point
// just before deferreturn was called.  The effect is that deferreturn
// is called again and again until there are no more deferred functions.
// Cannot split the stack because we reuse the caller's frame to
// call the deferred function.

// The single argument isn't actually used - it just has its address
// taken so it can be matched against pending defers.
#pragma textflag NOSPLIT
void
runtime·deferreturn(uintptr arg0)
{
	Defer *d;
	byte *argp;
	FuncVal *fn;

	d = g->defer;
	if(d == nil)
		return;
	argp = (byte*)&arg0; ///第一个参数的地址

	// d->argp为调用者(即main.test)在调用deferproc之前的栈指针SP，通过比较这两个地址，就可以确定是否是同
	// 一个调用函数(即main.test)的栈。如果不同，说明Defer不属于当前调用函数，从而中断deferreturn的循环调用
	if(d->argp != argp)
		return;

	// Moving arguments around.
	// Do not allow preemption here, because the garbage collector
	// won't know the form of the arguments until the jmpdefer can
	// flip the PC over to fn.
	m->locks++;
	runtime·memmove(argp, d->args, d->siz);
	fn = d->fn;
	g->defer = d->link;
	freedefer(d); /// 释放Defer对象
	m->locks--;
	if(m->locks == 0 && g->preempt)
		g->stackguard0 = StackPreempt;

	// 执行defer.fn
	runtime·jmpdefer(fn, argp);
}
```

`runtime·deferproc`函数中并没有循环执行G.defer的逻辑。实际上，这个循环是通过`runtime·jmpdefer`递归调用实现的:

```
// void jmpdefer(fn, sp);
// called from deferreturn.
// 1. pop the caller
// 2. sub 5 bytes from the callers return
// 3. jmp to the argument
TEXT runtime·jmpdefer(SB), NOSPLIT, $0-16
	MOVQ	8(SP), DX	// fn
	MOVQ	16(SP), BX	// caller sp, 参数argp，也就是deferreturn的arg0的地址
	LEAQ	-8(BX), SP	// caller sp after CALL
	SUBQ	$5, (SP)	// return to CALL again
	MOVQ	0(DX), BX
	JMP	BX	// but first run the deferred function, 调用defer函数
```

BX是deferreturn的arg0的地址，该参数保存在caller(main.test)调用`runtime·deferreturn`前的栈顶。而-8(BX)保存的刚好是`runtime·deferreturn`执行完后的返回地址（`CALL`会执行`PUSH IP`），即main.test的0x2092。`SUBQ	$5, (SP)`减掉5使得保存在SP的返回地址刚好减掉了指令`callq  0x21970 <runtime.deferreturn>`的长度，即0x208d。至此，在执行`JMP BX`前，SP的值为指令`callq  0x21970 <runtime.deferreturn>`的地址，即0x208d。

当`runtime.deferreturn`执行完返回时，会执行`RET`指令，从SP取出返回地址，又重新执行`callq  0x21970 <runtime.deferreturn>`，重而实现了`runtime.deferreturn`的递归调用:

![](/assets/golang/stack-and-defer-01.jpg)

* stack in C

Go语言对函数的栈处理与C语言有些区别。在C中，在函数的开始都会将BP的值入栈，在返回之前，恢复BP的值。而Go语言的函数不会处理BP:

```
00000000004004c4 <test>:
  4004c4:       55                      push   %rbp
  4004c5:       48 89 e5                mov    %rsp,%rbp
  4004c8:       bf e8 05 40 00          mov    $0x4005e8,%edi
  4004cd:       e8 e6 fe ff ff          callq  4003b8 <puts@plt>
  4004d2:       c9                      leaveq  # 相当于 mov %rbp, %rsp; pop %rbp
  4004d3:       c3                      retq   

00000000004004d4 <main>:
  4004d4:       55                      push   %rbp
  4004d5:       48 89 e5                mov    %rsp,%rbp
  4004d8:       b8 00 00 00 00          mov    $0x0,%eax
  4004dd:       e8 e2 ff ff ff          callq  4004c4 <test>
  4004e2:       c9                      leaveq 
  4004e3:       c3                      retq  
```

## panic and recover

C++有try/catch异常处理机制，Go语言中也有类似的机制panic/recover。

* panic

一些运行时错误，比如数组越界、空指针等，会导致goroutine发生[panic](https://golang.org/ref/spec#Handling_panics)。也可以主动调用panic函数触发异常。

```
While executing a function F, an explicit call to panic or a run-time panic terminates the execution
of F. Any functions deferred by F are then executed as usual. Next, any deferred functions run by 
F's caller are run, and so on up to any deferred by the top-level function in the executing 
goroutine. At that point, the program is terminated and the error condition is reported, including
the value of the argument to panic. This termination sequence is called panicking. 
```

如果函数F发生panic，F中的defer函数仍然会执行，调用F的函数中的defer也会执行，直到goroutine上最上层函数。然后goroutine结束，并报告相应的错误。这时，defer的作用有点类似C++中的finally的作用。

```go
package main

import "fmt"

func main() {
	c := make(chan int)
	go func() {
		defer func() {
			c <- 1
		}()
		f()
	}()
	<-c
	fmt.Println("main done")
}

func f() {
	fmt.Println("In f")
	g()
	fmt.Println("Exit f")
}

func g() {
	defer func() {
		fmt.Println("Gefer in g")
	}()
	panic("Panic in g")

	fmt.Println("Exit g")
}
```

```
In f
Gefer in g
panic: Panic in g

goroutine 5 [running]:
main.g()
	/Users/yy/dev/go/src/github.com/hustcat/golangexample/c/panic_ex2.go:27 +0x86
main.f()
	/Users/yy/dev/go/src/github.com/hustcat/golangexample/c/panic_ex2.go:19 +0xe3
main.main.func1(0x8201f00c0)
	/Users/yy/dev/go/src/github.com/hustcat/golangexample/c/panic_ex2.go:11 +0x3f
created by main.main
	/Users/yy/dev/go/src/github.com/hustcat/golangexample/c/panic_ex2.go:12 +0x5a

goroutine 1 [runnable]:
main.main()
	/Users/yy/dev/go/src/github.com/hustcat/golangexample/c/panic_ex2.go:13 +0x7d
exit status 2
```

* recover

goroutine的panic导致整个进程都crash。很多时候，我们希望单个goroutine的异常不要让整个进程都crash，`recover`可以实现这个目的:

```
Recover is a built-in function that regains control of a panicking goroutine. Recover is only useful inside deferred functions. During normal execution, a call to recover will return nil and have no other effect. If the current goroutine is panicking, a call to recover will capture the value given to panic and resume normal execution.
```

```go
func main() {
	c := make(chan int)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				fmt.Println("Recovered in goroutine: ", r)
			}
			c <- 1
		}()
		f()
	}()
	<-c
	fmt.Println("main done")
}
...
```

这时，main函数可以正常结束:

```
In f
Gefer in g
Recovered in goroutine:  Panic in g
main done
```

* runtime.panic

```
// go 1.3
(gdb) disass
Dump of assembler code for function main.g:
=> 0x0000000000002290 <+0>:	mov    %gs:0x8a0,%rcx
   0x0000000000002299 <+9>:	cmp    (%rcx),%rsp
   0x000000000000229c <+12>:	ja     0x22a5 <main.g+21>
   0x000000000000229e <+14>:	callq  0x284c0 <runtime.morestack00_noctxt>
   0x00000000000022a3 <+19>:	jmp    0x2290 <main.g>
   0x00000000000022a5 <+21>:	sub    $0x40,%rsp
   0x00000000000022a9 <+25>:	mov    $0xf26f8,%ecx
   0x00000000000022ae <+30>:	push   %rcx
   0x00000000000022af <+31>:	pushq  $0x0
   0x00000000000022b1 <+33>:	callq  0xfd90 <runtime.deferproc> #创建Defer对象
   0x00000000000022b6 <+38>:	pop    %rcx
   0x00000000000022b7 <+39>:	pop    %rcx
   0x00000000000022b8 <+40>:	test   %rax,%rax
   0x00000000000022bb <+43>:	jne    0x2307 <main.g+119>
   0x00000000000022bd <+45>:	lea    0xceec0,%rbx
   0x00000000000022c5 <+53>:	mov    (%rbx),%rbp
   0x00000000000022c8 <+56>:	mov    %rbp,0x30(%rsp)
   0x00000000000022cd <+61>:	mov    0x8(%rbx),%rbp
   0x00000000000022d1 <+65>:	mov    %rbp,0x38(%rsp)
   0x00000000000022d6 <+70>:	movq   $0x974c0,(%rsp)
   0x00000000000022de <+78>:	lea    0x30(%rsp),%rbx
   0x00000000000022e3 <+83>:	mov    %rbx,0x8(%rsp)
   0x00000000000022e8 <+88>:	callq  0x20d30 <runtime.convT2E>
   0x00000000000022ed <+93>:	lea    0x10(%rsp),%rbx
   0x00000000000022f2 <+98>:	lea    (%rsp),%rbp
   0x00000000000022f6 <+102>:	mov    %rbp,%rdi
   0x00000000000022f9 <+105>:	mov    %rbx,%rsi
   0x00000000000022fc <+108>:	movsq  %ds:(%rsi),%es:(%rdi)
   0x00000000000022fe <+110>:	movsq  %ds:(%rsi),%es:(%rdi)
   0x0000000000002300 <+112>:	callq  0x10080 <runtime.panic> #调用runtime.panic
   0x0000000000002305 <+117>:	ud2    
   0x0000000000002307 <+119>:	nop
   0x0000000000002308 <+120>:	callq  0xfe00 <runtime.deferreturn>
   0x000000000000230d <+125>:	add    $0x40,%rsp
   0x0000000000002311 <+129>:	retq   

```


```
// Called from panic.  Mimics morestack,
// reuses stack growth code to create a frame
// with the desired args running the desired function.
//
// func call(fn *byte, arg *byte, argsize uint32).
TEXT runtime·newstackcall(SB), NOSPLIT, $0-20
	get_tls(CX)
	MOVQ	m(CX), BX   ## m -> BX

	// Save our caller's state as the PC and SP to
	// restore when returning from f.
	MOVQ	0(SP), AX	// our caller's PC  ## ret IP -> M.morebuf.pc
	MOVQ	AX, (m_morebuf+gobuf_pc)(BX)
	LEAQ	8(SP), AX	// our caller's SP  ## caller's SP -> M.morebuf.sp
	MOVQ	AX, (m_morebuf+gobuf_sp)(BX)
	MOVQ	g(CX), AX						## g -> AX
	MOVQ	AX, (m_morebuf+gobuf_g)(BX)     ## g -> M.morebuf.g
	
	// Save our own state as the PC and SP to restore
	// if this goroutine needs to be restarted.
	MOVQ	$runtime·newstackcall(SB), (g_sched+gobuf_pc)(AX) ## ret IP -> g.sched.pc
	MOVQ	SP, (g_sched+gobuf_sp)(AX)	## SP -> g.sched.sp

	// Set up morestack arguments to call f on a new stack.
	// We set f's frame size to 1, as a hint to newstack
	// that this is a call from runtime·newstackcall.
	// If it turns out that f needs a larger frame than
	// the default stack, f's usual stack growth prolog will
	// allocate a new segment (and recopy the arguments).
	MOVQ	8(SP), AX	// fn
	MOVQ	16(SP), DX	// arg frame
	MOVL	24(SP), CX	// arg size

	MOVQ	AX, m_cret(BX)	// f's PC  ## fn -> M.cret
	MOVQ	DX, m_moreargp(BX)	// argument frame pointer
	MOVL	CX, m_moreargsize(BX)	// f's argument size
	MOVL	$1, m_moreframesize(BX)	// f's frame size

	// Call newstack on m->g0's stack.
	MOVQ	m_g0(BX), BP  ## m->g0 -> BP
	get_tls(CX)
	MOVQ	BP, g(CX) ## g0 -> g
	MOVQ	(g_sched+gobuf_sp)(BP), SP ##g0.sched.sp -> SP, SP指向了g0的栈
	CALL	runtime·newstack(SB)
	MOVQ	$0, 0x1103	// crash if newstack returns
	RET
```

## Reference

* [Defer, Panic, and Recover](https://blog.golang.org/defer-panic-and-recover)
* [How Stacks are Handled in Go](https://blog.cloudflare.com/how-stacks-are-handled-in-go/)
* [3.5 连续栈](https://tiancaiamao.gitbooks.io/go-internals/content/zh/03.5.html)
* [gdb Debugging Full Example (Tutorial): ncurses](http://www.brendangregg.com/blog/2016-08-09/gdb-example-ncurses.html)