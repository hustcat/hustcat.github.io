---
layout: post
title: Dive into closure in go
date: 2016-08-19 15:00:30
categories: 编程语言
tags: golang
excerpt: Dive into closure in go
---

## escape analysis

在Go语言中，编译器使用[escape analysis](https://en.wikipedia.org/wiki/Escape_analysis)，对于函数中的局部变量，会尽量从stack上分配；但是，如果函数返回之后，还有对这个变量的引用，这个变量就会从[heap分配](https://golang.org/doc/faq#stack_or_heap)。

```
When possible, the Go compilers will allocate variables that are local to a function in that 
function's stack frame. However, if the compiler cannot prove that the variable is not referenced
after the function returns, then the compiler must allocate the variable on the garbage-collected
heap to avoid dangling pointer errors. Also, if a local variable is very large, it might make more
sense to store it on the heap rather than the stack. 
```

考虑如下程序:

```go
func f() *int{
    i := 1
    return &i
}

func main(){
    i := f()
    fmt.Println(*i)
}
```


```sh
$ go build -gcflags "-l -m" test.go  #-l 禁用函数内联，-m 输出优化信息
# command-line-arguments
./test.go:6: moved to heap: i
./test.go:7: &i escapes to heap
./test.go:12: main ... argument does not escape

$ go tool 6g -S test.go
"".f t=1 size=64 value=0 args=0x8 locals=0x10
        000000 00000 (test.go:5)        TEXT    "".f+0(SB),$16-8
        000000 00000 (test.go:5)        MOVQ    (TLS),CX
        0x0009 00009 (test.go:5)        CMPQ    SP,(CX)
        0x000c 00012 (test.go:5)        JHI     ,21
        0x000e 00014 (test.go:5)        CALL    ,runtime.morestack8_noctxt(SB)
        0x0013 00019 (test.go:5)        JMP     ,0
        0x0015 00021 (test.go:5)        SUBQ    $16,SP
        0x0019 00025 (test.go:5)        FUNCDATA        $2,gclocals·a7a3692b8e27e823add69ec4239ba55f+0(SB)
        0x0019 00025 (test.go:5)        FUNCDATA        $3,gclocals·3280bececceccd33cb74587feedb1f9f+0(SB)
        0x0019 00025 (test.go:6)        MOVQ    $type.int+0(SB),(SP) # 传给runtime.new的参数
        0x0021 00033 (test.go:6)        PCDATA  $0,$16
        0x0021 00033 (test.go:6)        PCDATA  $1,$0
        0x0021 00033 (test.go:6)        CALL    ,runtime.new(SB) # new(int)
        0x0026 00038 (test.go:6)        PCDATA  $0,$-1
        0x0026 00038 (test.go:6)        MOVQ    8(SP),AX
        0x002b 00043 (test.go:6)        MOVQ    $1,(AX) # 1 -> i
        0x0032 00050 (test.go:7)        MOVQ    AX,"".~r0+24(FP)
        0x0037 00055 (test.go:7)        ADDQ    $16,SP
        0x003b 00059 (test.go:7)        RET     ,
```

```
(gdb) disass
Dump of assembler code for function main.main:
   0x0000000000002060 <+0>:	mov    %gs:0x8a0,%rcx
   0x0000000000002069 <+9>:	cmp    (%rcx),%rsp
   0x000000000000206c <+12>:	ja     0x2075 <main.main+21>
   0x000000000000206e <+14>:	callq  0x27db0 <runtime.morestack00_noctxt>
   0x0000000000002073 <+19>:	jmp    0x2060 <main.main>
   0x0000000000002075 <+21>:	sub    $0x80,%rsp
   0x000000000000207c <+28>:	callq  0x2000 <main.f>
=> 0x0000000000002081 <+33>:	mov    (%rsp),%rbx # main.main.i
...
Dump of assembler code for function main.f:
   0x0000000000002000 <+0>:	mov    %gs:0x8a0,%rcx
   0x0000000000002009 <+9>:	cmp    (%rcx),%rsp
   0x000000000000200c <+12>:	ja     0x2015 <main.f+21>
   0x000000000000200e <+14>:	callq  0x27de0 <runtime.morestack8_noctxt>
   0x0000000000002013 <+19>:	jmp    0x2000 <main.f>
   0x0000000000002015 <+21>:	sub    $0x18,%rsp
   0x0000000000002019 <+25>:	movq   $0x0,0x20(%rsp) # 0 -> main.main.i
=> 0x0000000000002022 <+34>:	movq   $0x967e0,(%rsp) # runtime.new的参数
   0x000000000000202a <+42>:	callq  0x22cb0 <runtime.new>
   0x000000000000202f <+47>:	mov    0x8(%rsp),%rbx  # SP+0x8保存runtime.new返回的地址
   0x0000000000002034 <+52>:	mov    %rbx,0x10(%rsp) # 将runtime.new返回的值赋值给SP+0x10(即&main.f.i)
   0x0000000000002039 <+57>:	mov    0x10(%rsp),%rbx # 1 -> main.f.i
   0x000000000000203e <+62>:	movq   $0x1,(%rbx)
   0x0000000000002045 <+69>:	mov    0x10(%rsp),%rbx # &i -> main.main.i(SP+0x20)
   0x000000000000204a <+74>:	mov    %rbx,0x20(%rsp)
   0x000000000000204f <+79>:	add    $0x18,%rsp
   0x0000000000002053 <+83>:	retq   
...
(gdb) disass
Dump of assembler code for function runtime.new:
   0x0000000000022cb0 <+0>:	sub    $0x18,%rsp
   0x0000000000022cb4 <+4>:	mov    0x20(%rsp),%rbx
=> 0x0000000000022cb9 <+9>:	movq   $0x0,0x28(%rsp)
   0x0000000000022cc2 <+18>:	mov    (%rbx),%rcx
   0x0000000000022cc5 <+21>:	mov    %rcx,(%rsp)
   0x0000000000022cc9 <+25>:	mov    %rbx,0x8(%rsp)
   0x0000000000022cce <+30>:	movzbl 0xf(%rbx),%edx
   0x0000000000022cd2 <+34>:	and    $0x80,%edx
   0x0000000000022cd8 <+40>:	cmp    $0x0,%edx
   0x0000000000022cdb <+43>:	je     0x22cf5 <runtime.new+69>
   0x0000000000022cdd <+45>:	mov    $0x1,%ecx
   0x0000000000022ce2 <+50>:	mov    %ecx,0x10(%rsp)
   0x0000000000022ce6 <+54>:	callq  0x21460 <runtime.mallocgc>
   0x0000000000022ceb <+59>:	mov    %rax,0x28(%rsp) ## runtime.mallocgc返回值赋给main.f中SP+0x8
   0x0000000000022cf0 <+64>:	add    $0x18,%rsp
   0x0000000000022cf4 <+68>:	retq   
```


```c
func new(typ *Type) (ret *uint8) {
	ret = runtime·mallocgc(typ->size, (uintptr)typ | TypeInfo_SingleObject, typ->kind&KindNoPointers ? FlagNoScan : 0);
}
```





## closure

Go语言通过闭包(closure)实现匿名函数，匿名函数可以直接赋值给一个变量，或者直接执行。在函数中定义的匿名函数可以访问该函数内部的变量:

```go
func add() func() int {
    i := 0
    return func() int {
        i++
        return i
    }
}

func main(){
    f := add()
    i := f() // 1
    i = f()  // 2
}
```

所以，闭包不仅是一段代码块，还包括一些引用的变量，这些变量在定义代码块的环境中定义。可以用下面的闭包结构体来描述上面的匿名函数:

```
type Closure struct {
    F func()() 
    i *int
}
```

```
"".add t=1 size=112 value=0 args=0x10 locals=0x18
        000000 00000 (ex2.go:5) TEXT    "".add+0(SB),$24-16
        000000 00000 (ex2.go:5) MOVQ    (TLS),CX
        0x0009 00009 (ex2.go:5) CMPQ    SP,(CX)
        0x000c 00012 (ex2.go:5) JHI     ,21
        0x000e 00014 (ex2.go:5) CALL    ,runtime.morestack16_noctxt(SB)
        0x0013 00019 (ex2.go:5) JMP     ,0
        0x0015 00021 (ex2.go:5) SUBQ    $24,SP
        0x0019 00025 (ex2.go:5) FUNCDATA        $2,gclocals·0666c54e355770f45aff0c97a488866f+0(SB)
        0x0019 00025 (ex2.go:5) FUNCDATA        $3,gclocals·e1ae6533a9e39048ba0735a2264ce16a+0(SB)
        0x0019 00025 (ex2.go:5) MOVQ    $type.int+0(SB),(SP)
        0x0021 00033 (ex2.go:5) PCDATA  $0,$16
        0x0021 00033 (ex2.go:5) PCDATA  $1,$0
        0x0021 00033 (ex2.go:5) CALL    ,runtime.new(SB) ## 给i分配空间
        0x0026 00038 (ex2.go:5) PCDATA  $0,$-1
        0x0026 00038 (ex2.go:5) MOVQ    8(SP),AX
        0x002b 00043 (ex2.go:5) MOVQ    AX,"".&i+16(SP)
        0x0030 00048 (ex2.go:5) MOVQ    "".i+32(FP),BP
        0x0035 00053 (ex2.go:5) MOVQ    BP,(AX)
        0x0038 00056 (ex2.go:9) MOVQ    $type.struct { F uintptr; A0 *int }+0(SB),(SP) # 闭包结构
        0x0040 00064 (ex2.go:9) PCDATA  $0,$16
        0x0040 00064 (ex2.go:9) PCDATA  $1,$1
        0x0040 00064 (ex2.go:9) CALL    ,runtime.new(SB) #分配closure对象
        0x0045 00069 (ex2.go:9) PCDATA  $0,$-1
        0x0045 00069 (ex2.go:9) MOVQ    8(SP),AX
        0x004a 00074 (ex2.go:9) NOP     ,
        0x004a 00074 (ex2.go:9) MOVQ    $"".func·001+0(SB),BP ##匿名函数赋给closure.F
        0x004f 00079 (ex2.go:9) MOVQ    BP,(AX)
        0x0052 00082 (ex2.go:9) NOP     ,
        0x0052 00082 (ex2.go:9) MOVQ    "".&i+16(SP),BP # &i -> closure.i
        0x0057 00087 (ex2.go:9) MOVQ    BP,8(AX)
        0x005b 00091 (ex2.go:9) MOVQ    AX,"".~r1+40(FP)
        0x0060 00096 (ex2.go:9) ADDQ    $24,SP
        0x0064 00100 (ex2.go:9) RET     ,
...

"".func·001 t=1 size=32 value=0 args=0x8 locals=0
        000000 00000 (ex2.go:6) TEXT    "".func·001+0(SB),68,$0-8
        000000 00000 (ex2.go:6) NOP     ,
        000000 00000 (ex2.go:6) NOP     ,
        000000 00000 (ex2.go:6) FUNCDATA        $2,gclocals·a7a3692b8e27e823add69ec4239ba55f+0(SB)
        000000 00000 (ex2.go:6) FUNCDATA        $3,gclocals·3280bececceccd33cb74587feedb1f9f+0(SB)
        000000 00000 (ex2.go:6) MOVQ    8(DX),AX
        0x0004 00004 (ex2.go:7) MOVQ    (AX),BP
        0x0007 00007 (ex2.go:7) INCQ    ,BP
        0x000a 00010 (ex2.go:7) MOVQ    BP,(AX)
        0x000d 00013 (ex2.go:7) NOP     ,
        0x000d 00013 (ex2.go:8) MOVQ    (AX),BP
        0x0010 00016 (ex2.go:8) MOVQ    BP,"".~r0+8(FP)
        0x0015 00021 (ex2.go:8) RET     ,
```

可以看到，匿名函数引用的变量，都是从heap分配。

## Reference

* [Escape Analysis in Go](https://scvalex.net/posts/29/)
* [Go Escape Analysis Flaws](https://docs.google.com/document/d/1CxgUBPlx9iJzkz9JWkb6tIpTe5q32QDmz8l0BouG0Cw/preview#heading=h.jf07ptqo2xbk)
* [Memory Management in Go](https://dougrichardson.org/2016/01/23/go-memory-allocations.html)
* [3.6 闭包的实现](https://tiancaiamao.gitbooks.io/go-internals/content/zh/03.6.html)