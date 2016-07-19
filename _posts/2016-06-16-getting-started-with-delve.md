---
layout: post
title: Getting started with Delve
date: 2016-06-16 11:00:30
categories: 编程语言
tags: golang
excerpt: Getting started with Delve
---

[Delve](https://github.com/derekparker/delve) aims to solve the various issues felt by developers when debugging their Go code with traditional tools such as GDB.

We must have Go 1.5 or higher installed. Also, if using Go 1.5 you must set `GO15VENDOREXPERIMENT=1` before attempting to install. Such as:

```sh
# export GO15VENDOREXPERIMENT=1
# export GOROOT=/usr/local/go_1.5.2/
# export PATH=/usr/local/go_1.5.2/bin:$PATH
```

* install delve

```
# mkdir -p vendor/src/github.com/derekparker
# ln -s /data/go/src/github.com/derekparker/delve vendor/src/github.com/derekparker/delve
# export GOPATH=/data/go/src/github.com/derekparker/delve/vendor
# go get github.com/derekparker/delve/cmd/dlv
# ls vendor/bin/
dlv
# cp vendor/bin/dlv /usr/local/bin/
```

* test program:

```go
package main

import (
	"fmt"
	"sync"
)

func dostuff(wg *sync.WaitGroup, i int) {
	fmt.Printf("goroutine id %d\n", i)
	fmt.Printf("goroutine id %d\n", i)
	wg.Done()
}

func main() {
	var wg sync.WaitGroup
	workers := 10

	wg.Add(workers)
	for i := 0; i < workers; i++ {
		go dostuff(&wg, i)
	}
	wg.Wait()
}
```

* Begin a debug session:

```
# dlv debug
Type 'help' for list of commands.
(dlv) help
The following commands are available:
    args ------------------------ Print function arguments.
    break (alias: b) ------------ Sets a breakpoint.
    breakpoints (alias: bp) ----- Print out info for active breakpoints.
    clear ----------------------- Deletes breakpoint.
    clearall -------------------- Deletes multiple breakpoints.
    condition (alias: cond) ----- Set breakpoint condition.
    continue (alias: c) --------- Run until breakpoint or program termination.
    disassemble (alias: disass) - Disassembler.
    exit (alias: quit | q) ------ Exit the debugger.
    frame ----------------------- Executes command on a different frame.
    funcs ----------------------- Print list of functions.
    goroutine ------------------- Shows or changes current goroutine
    goroutines ------------------ List program goroutines.
    help (alias: h) ------------- Prints the help message.
    list (alias: ls) ------------ Show source code.
    locals ---------------------- Print local variables.
    next (alias: n) ------------- Step over to next source line.
    on -------------------------- Executes a command when a breakpoint is hit.
    print (alias: p) ------------ Evaluate an expression.
    regs ------------------------ Print contents of CPU registers.
    restart (alias: r) ---------- Restart process.
    set ------------------------- Changes the value of a variable.
    source ---------------------- Executes a file containing a list of delve commands
    sources --------------------- Print list of source files.
    stack (alias: bt) ----------- Print stack trace.
    step (alias: s) ------------- Single step through program.
    step-instruction (alias: si)  Single step a single cpu instruction.
    thread (alias: tr) ---------- Switch to the specified thread.
    threads --------------------- Print out info for every traced thread.
    trace (alias: t) ------------ Set tracepoint.
    types ----------------------- Print list of types
    vars ------------------------ Print package variables.
Type help followed by a command for full documentation.
```

* Set breakpoint:

```
(dlv) break main.main
Breakpoint 1 set at 0x401283 for main.main() ./test.go:14
```

* Continue to breakpoint:

```
(dlv) continue
> main.main() ./test.go:14 (hits goroutine(1):1 total:1) (PC: 0x401283)
     9:         fmt.Printf("goroutine id %d\n", i)
    10:         fmt.Printf("goroutine id %d\n", i)
    11:         wg.Done()
    12: }
    13:
=>  14: func main() {
    15:         var wg sync.WaitGroup
    16:         workers := 10
    17:
    18:         wg.Add(workers)
    19:         for i := 0; i < workers; i++ {
```

* Step over to next source line:

```
(dlv) next
> main.main() ./test.go:15 (PC: 0x401287)
    10:         fmt.Printf("goroutine id %d\n", i)
    11:         wg.Done()
    12: }
    13:
    14: func main() {
=>  15:         var wg sync.WaitGroup
    16:         workers := 10
    17:
    18:         wg.Add(workers)
    19:         for i := 0; i < workers; i++ {
20:                 go dostuff(&wg, i)
```

* Print variables:

```
(dlv) print workers
10
```

* List program goroutines:

```
(dlv) break dostuff
Breakpoint 2 set at 0x401018 for main.dostuff() ./test.go:8
(dlv) continue
(dlv) goroutines
[14 goroutines]
  Goroutine 1 - User: /usr/local/go/src/runtime/sema.go:43 sync.runtime_Semacquire (0x436ab3)
  Goroutine 2 - User: /usr/local/go/src/runtime/proc.go:186 runtime.gopark (0x4297f3)
  Goroutine 3 - User: /usr/local/go/src/runtime/proc.go:186 runtime.gopark (0x4297f3)
  Goroutine 4 - User: /usr/local/go/src/runtime/proc.go:186 runtime.gopark (0x4297f3)
* Goroutine 5 - User: ./test.go:8 main.dostuff (0x401018)
  Goroutine 6 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 7 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 8 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 9 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 10 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 11 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 12 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 13 - User: ./test.go:8 main.dostuff (0x401000)
  Goroutine 14 - User: ./test.go:8 main.dostuff (0x401018)

All goroutine pause at function `dostuff`.
```

* Print stack trace：

```
(dlv) bt
0  0x0000000000401018 in main.dostuff
   at ./test.go:8
1  0x0000000000455bb0 in runtime.goexit
   at /usr/local/go/src/runtime/asm_amd64.s:1721
```

* delete breakpoints:

```
(dlv) breakpoints
Breakpoint unrecovered-panic at 0x4271e0 for runtime.startpanic() /usr/local/go/src/runtime/panic.go:504 (0)
Breakpoint 1 at 0x401283 for main.main() ./test.go:14 (1)
Breakpoint 2 at 0x401018 for main.dostuff() ./test.go:8 (2)
(dlv) clear 1
Breakpoint 1 cleared at 0x401283 for main.main() ./test.go:14
(dlv) breakpoints
Breakpoint unrecovered-panic at 0x4271e0 for runtime.startpanic() /usr/local/go/src/runtime/panic.go:504 (0)
Breakpoint 2 at 0x401018 for main.dostuff() ./test.go:8 (2)


(dlv) quit
Process 14068 has exited with status 0
```

* debug with arguments

```sh
# dlv debug . -- -key=value
```

### Reference

* [Debugging Go programs with Delve](https://blog.gopheracademy.com/advent-2015/debugging-with-delve/)
* [Installation on Linux](https://github.com/derekparker/delve/blob/master/Documentation/installation/linux/install.md)
* [Debugging Go Code with GDB](https://golang.org/doc/gdb)
* [how to pass arguments to the running program](https://github.com/derekparker/delve/issues/178)
