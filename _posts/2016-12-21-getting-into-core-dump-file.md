---
layout: post
title: Getting into the Linux ELF and core dump file
date: 2016-12-21 17:00:30
categories: Linux
tags: elf
excerpt: Getting into the Linux ELF and core dump file
---

最近遇到一个业务发生coredump文件不完整的问题，稍微深入的研究了一下ELF和coredump文件。

## ELF format

[ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)的基本结构如下：

![](/assets/elf/Elf-layout.png)

每个ELF文件主要分`ELF header`、`Program header table`、`Section header table`以及相对应的`Program/Section table entry`指向的程序或者控制数据。

ELF文件有两种视图，`Execution View`和`Linking View`:

![](/assets/elf/elf-view.png)

### ELF header

ELF header位于文件的头部，用于描述文件的整体结构：

```c
typedef struct elf64_hdr {
  unsigned char	e_ident[EI_NIDENT];	/* ELF "magic number" 16 bytes*/
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;		/* Entry point virtual address */
  Elf64_Off e_phoff;		/* Program header table file offset */
  Elf64_Off e_shoff;		/* Section header table file offset */
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;      ///elf header size
  Elf64_Half e_phentsize;   ///program header entry size
  Elf64_Half e_phnum;       ///program header count
  Elf64_Half e_shentsize;   ///section header entry size
  Elf64_Half e_shnum;		///section header count
  Elf64_Half e_shstrndx;
} Elf64_Ehdr;
```

查看可执行文件的ELF header：

```sh
# readelf -h test 
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x400470
  Start of program headers:          64 (bytes into file)
  Start of section headers:          2680 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         8
  Size of section headers:           64 (bytes)
  Number of section headers:         30
  Section header string table index: 27
``` 

* Program headers

`Program header`用于内核构造进程的内存镜像，对应到`/proc/$PID/maps`：

```c
typedef struct elf64_phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;		/* Segment file offset */
  Elf64_Addr p_vaddr;		/* Segment virtual address */
  Elf64_Addr p_paddr;		/* Segment physical address */
  Elf64_Xword p_filesz;		/* Segment size in file */
  Elf64_Xword p_memsz;		/* Segment size in memory */
  Elf64_Xword p_align;		/* Segment alignment, file & memory */
} Elf64_Phdr;
```

查看执行文件的program headers：

```sh
# readelf -l test  

Elf file type is EXEC (Executable file)
Entry point 0x400470
There are 8 program headers, starting at offset 64

Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  PHDR           0x0000000000000040 0x0000000000400040 0x0000000000400040
                 0x00000000000001c0 0x00000000000001c0  R E    8
  INTERP         0x0000000000000200 0x0000000000400200 0x0000000000400200
                 0x000000000000001c 0x000000000000001c  R      1
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  LOAD           0x0000000000000000 0x0000000000400000 0x0000000000400000
                 0x000000000000074c 0x000000000000074c  R E    200000
  LOAD           0x0000000000000750 0x0000000000600750 0x0000000000600750
                 0x00000000000001fc 0x0000000000000210  RW     200000
  DYNAMIC        0x0000000000000778 0x0000000000600778 0x0000000000600778
                 0x0000000000000190 0x0000000000000190  RW     8
  NOTE           0x000000000000021c 0x000000000040021c 0x000000000040021c
                 0x0000000000000044 0x0000000000000044  R      4
  GNU_EH_FRAME   0x00000000000006a8 0x00000000004006a8 0x00000000004006a8
                 0x0000000000000024 0x0000000000000024  R      4
  GNU_STACK      0x0000000000000000 0x0000000000000000 0x0000000000000000
                 0x0000000000000000 0x0000000000000000  RW     8

 Section to Segment mapping:
  Segment Sections...
   00     
   01     .interp 
   02     .interp .note.ABI-tag .note.gnu.build-id .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn .rela.plt .init .plt .text .fini .rodata .eh_frame_hdr .eh_frame 
   03     .ctors .dtors .jcr .dynamic .got .got.plt .data .bss 
   04     .dynamic 
   05     .note.ABI-tag .note.gnu.build-id 
   06     .eh_frame_hdr 
   07     
```

* Sections headers

 Sections hold the bulk of object file information for the linking view: instructions, data, symbol table, relocation information, and so on.

```c
typedef struct elf64_shdr {
  Elf64_Word sh_name;		/* Section name, index in string tbl */
  Elf64_Word sh_type;		/* Type of section */
  Elf64_Xword sh_flags;		/* Miscellaneous section attributes */
  Elf64_Addr sh_addr;		/* Section virtual addr at execution */
  Elf64_Off sh_offset;		/* Section file offset */
  Elf64_Xword sh_size;		/* Size of section in bytes */
  Elf64_Word sh_link;		/* Index of another section */
  Elf64_Word sh_info;		/* Additional section information */
  Elf64_Xword sh_addralign;	/* Section alignment */
  Elf64_Xword sh_entsize;	/* Entry size if section holds table */
} Elf64_Shdr;
```

查看执行文件的section headers：

```sh
# readelf -S test  
There are 30 section headers, starting at offset 0xa78:

Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [ 0]                   NULL             0000000000000000  00000000
       0000000000000000  0000000000000000           0     0     0
  [ 1] .interp           PROGBITS         0000000000400200  00000200
       000000000000001c  0000000000000000   A       0     0     1
  [ 2] .note.ABI-tag     NOTE             000000000040021c  0000021c
       0000000000000020  0000000000000000   A       0     0     4
  [ 3] .note.gnu.build-i NOTE             000000000040023c  0000023c
       0000000000000024  0000000000000000   A       0     0     4
  [ 4] .gnu.hash         GNU_HASH         0000000000400260  00000260
       000000000000001c  0000000000000000   A       5     0     8
  [ 5] .dynsym           DYNSYM           0000000000400280  00000280
       0000000000000090  0000000000000018   A       6     1     8
  [ 6] .dynstr           STRTAB           0000000000400310  00000310
       000000000000004e  0000000000000000   A       0     0     1
  [ 7] .gnu.version      VERSYM           000000000040035e  0000035e
       000000000000000c  0000000000000002   A       5     0     2
  [ 8] .gnu.version_r    VERNEED          0000000000400370  00000370
       0000000000000020  0000000000000000   A       6     1     8
  [ 9] .rela.dyn         RELA             0000000000400390  00000390
       0000000000000018  0000000000000018   A       5     0     8
  [10] .rela.plt         RELA             00000000004003a8  000003a8
       0000000000000060  0000000000000018   A       5    12     8
  [11] .init             PROGBITS         0000000000400408  00000408
       0000000000000018  0000000000000000  AX       0     0     4
  [12] .plt              PROGBITS         0000000000400420  00000420
       0000000000000050  0000000000000010  AX       0     0     4
  [13] .text             PROGBITS         0000000000400470  00000470
       0000000000000218  0000000000000000  AX       0     0     16
  [14] .fini             PROGBITS         0000000000400688  00000688
       000000000000000e  0000000000000000  AX       0     0     4
  [15] .rodata           PROGBITS         0000000000400698  00000698
       0000000000000010  0000000000000000   A       0     0     8
  [16] .eh_frame_hdr     PROGBITS         00000000004006a8  000006a8
       0000000000000024  0000000000000000   A       0     0     4
  [17] .eh_frame         PROGBITS         00000000004006d0  000006d0
       000000000000007c  0000000000000000   A       0     0     8
  [18] .ctors            PROGBITS         0000000000600750  00000750
       0000000000000010  0000000000000000  WA       0     0     8
  [19] .dtors            PROGBITS         0000000000600760  00000760
       0000000000000010  0000000000000000  WA       0     0     8
  [20] .jcr              PROGBITS         0000000000600770  00000770
       0000000000000008  0000000000000000  WA       0     0     8
  [21] .dynamic          DYNAMIC          0000000000600778  00000778
       0000000000000190  0000000000000010  WA       6     0     8
  [22] .got              PROGBITS         0000000000600908  00000908
       0000000000000008  0000000000000008  WA       0     0     8
  [23] .got.plt          PROGBITS         0000000000600910  00000910
       0000000000000038  0000000000000008  WA       0     0     8
  [24] .data             PROGBITS         0000000000600948  00000948
       0000000000000004  0000000000000000  WA       0     0     4
  [25] .bss              NOBITS           0000000000600950  0000094c
       0000000000000010  0000000000000000  WA       0     0     8
  [26] .comment          PROGBITS         0000000000000000  0000094c
       000000000000002c  0000000000000001  MS       0     0     1
  [27] .shstrtab         STRTAB           0000000000000000  00000978
       00000000000000fe  0000000000000000           0     0     1
  [28] .symtab           SYMTAB           0000000000000000  000011f8
       0000000000000630  0000000000000018          29    46     8
  [29] .strtab           STRTAB           0000000000000000  00001828
       000000000000021b  0000000000000000           0     0     1
Key to Flags:
  W (write), A (alloc), X (execute), M (merge), S (strings)
  I (info), L (link order), G (group), x (unknown)
  O (extra OS processing required) o (OS specific), p (processor specific)
```

## Core dump file

当进程收到，比如`SIGABRT`时，会进行coredump操作：

```c
int get_signal_to_deliver(siginfo_t *info, struct k_sigaction *return_ka,
			  struct pt_regs *regs, void *cookie)
{
///...
		if (sig_kernel_coredump(signr)) {
			if (print_fatal_signals)
				print_fatal_signal(info->si_signo);
			proc_coredump_connector(current);
			/*
			 * If it was able to dump core, this kills all
			 * other threads in the group and synchronizes with
			 * their demise.  If we lost the race with another
			 * thread getting here, it set group_exit_code
			 * first and our do_group_exit call below will use
			 * that value and ignore the one we pass it.
			 */
			do_coredump(info);
		}

```

对于ELF执行程序，最终会调用`[elf_core_dump](https://bitbucket.org/hustcat/kernel-3.10.83/src/cf765cbd7202f226f5e6c1945dbf4fcea3bd6853/fs/binfmt_elf.c?at=master&fileviewer=file-view-default#binfmt_elf.c-2054)`：

ELF coredump文件主要包含4部分内容，按写入先后顺序：

(1)ELF header

```c
	size += sizeof(*elf);
	if (size > cprm->limit || !dump_write(cprm->file, elf, sizeof(*elf)))
		goto end_coredump;
```

(2) Program headers

```c
	size += sizeof(*phdr4note); ///hdr note
	if (size > cprm->limit
	    || !dump_write(cprm->file, phdr4note, sizeof(*phdr4note)))
		goto end_coredump;

	/* Write program headers for segments dump */
	for (vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma)) {
		struct elf_phdr phdr;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = vma->vm_start;
		phdr.p_paddr = 0;
		phdr.p_filesz = vma_dump_size(vma, cprm->mm_flags);
		phdr.p_memsz = vma->vm_end - vma->vm_start;
		offset += phdr.p_filesz;
		phdr.p_flags = vma->vm_flags & VM_READ ? PF_R : 0;
		if (vma->vm_flags & VM_WRITE)
			phdr.p_flags |= PF_W;
		if (vma->vm_flags & VM_EXEC)
			phdr.p_flags |= PF_X;
		phdr.p_align = ELF_EXEC_PAGESIZE;

		size += sizeof(phdr);
		if (size > cprm->limit
		    || !dump_write(cprm->file, &phdr, sizeof(phdr)))
			goto end_coredump;
	}
```

(3) 进程信息，包括signal、registers、以及task_struct数据等。

```c
 	/* write out the notes section */
	if (!write_note_info(&info, cprm->file, &foffset))
		goto end_coredump;
```

(4) 内存数据

```c
	/* Align to page */
	if (!dump_seek(cprm->file, dataoff - foffset))
		goto end_coredump;

	for (vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma)) {
		unsigned long addr;
		unsigned long end;

		end = vma->vm_start + vma_dump_size(vma, cprm->mm_flags);

		for (addr = vma->vm_start; addr < end; addr += PAGE_SIZE) {
			struct page *page;
			int stop;

			page = get_dump_page(addr);
			if (page) {
				void *kaddr = kmap(page);
				stop = ((size += PAGE_SIZE) > cprm->limit) ||
					!dump_write(cprm->file, kaddr,
						    PAGE_SIZE);
				kunmap(page);
				page_cache_release(page);
			} else
				stop = !dump_seek(cprm->file, PAGE_SIZE);
			if (stop)
				goto end_coredump;
		}
	}
```

可以用`readelf`查看coredump file的信息。当内核在执行coredump时，可能会被信号中断，从而引起coredump不完整(truncated)。

```c
/*
 * Core dumping helper functions.  These are the only things you should
 * do on a core-file: use only these functions to write out all the
 * necessary info.
 */
int dump_write(struct file *file, const void *addr, int nr)
{
	return !dump_interrupted() && ///收到信号，则停止dump
		access_ok(VERIFY_READ, addr, nr) &&
		file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}
```

## Trace signal send and deliver

内核有2个与signal相关的tracepoint，可以用来trace内核的信号发送与接收的情况：

```sh
# ./tpoint -l | grep signal
signal:signal_deliver
signal:signal_generate
```

* Trace signal deliver

```sh
# ps -ef|grep test_signal
root     32309 26674 89 10:42 ?        00:00:12 ./test_signal
root     32621  1418  0 10:43 pts/1    00:00:00 grep test_signal

# ./tpoint -p 32309 signal:signal_deliver                   
Tracing signal:signal_deliver. Ctrl-C to end.
     test_signal-32309 [019] 51151109.432816: signal_deliver: sig=27 errno=0 code=128 sa_handler=400634 sa_flags=14000004
           <...>-32309 [019] 51151110.433334: signal_deliver: sig=27 errno=0 code=128 sa_handler=400634 sa_flags=14000004
           <...>-32309 [019] 51151111.434852: signal_deliver: sig=27 errno=0 code=128 sa_handler=400634 sa_flags=14000004
^C
Ending tracing...
```

[测试程序](/assets/elf/test_signal.c)

* Trace signal send

内核信号的发送最终都会调用`__send_signal`:

```sh
static int __send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group, int from_ancestor_ns)
{
///...
ret:
	trace_signal_generate(sig, info, t, group, result);
	return ret;
}
```

```sh
# ./tpoint -p 32309 signal:signal_generate
Tracing signal:signal_generate. Ctrl-C to end.
     test_signal-32309 [011] 51151417.094942: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
     test_signal-32309 [011] 51151418.097461: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
           <...>-32309 [011] 51151419.099980: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
           <...>-32309 [011] 51151419.642213: signal_generate: sig=17 errno=0 code=262146 comm=bash pid=26674 grp=1 res=0
^C
Ending tracing...
```

可以看到，信号`SIGPROF(27)`是由进程自己发送给自己的，另外，当进程退出时，会给父进程发送`SIGCHLD(17)`。

## Interval timer

[setitimer](http://man7.org/linux/man-pages/man2/setitimer.2.html)用来设置[interval timers](http://www.ibm.com/developerworks/cn/linux/1308_liuming_linuxtime3/)。对于`ITIMER_PROF`定时器，当timer超时，内核会给进程发送`SIGPROF`信号。

> 早期 Linux 考虑两种定时器：内核自身需要的timer，也叫做动态定时器；其次是来自用户态的需要, 即 setitimer 
> 定时器，也叫做间隔定时器。2.5.63 开始支持 POSIX Timer。2.6.16 引入了高精度 [hrtimer](https://www.ibm.com/developerworks/cn/linux/1308_liuming_linuxtime4/)，而且所有其它的timer都是建立在hrtimer之上的。

数据结构：

```c
struct signal_struct {
///...
	/*
	 * ITIMER_PROF and ITIMER_VIRTUAL timers for the process, we use
	 * CPUCLOCK_PROF and CPUCLOCK_VIRT for indexing array as these
	 * values are defined to 0 and 1 respectively
	 */
	struct cpu_itimer it[2]; ///interval timers per-process, see set_cpu_itimer
```

内核函数调用栈：

```sh
# ./tpoint -s -p 10213 signal:signal_generate     
Tracing signal:signal_generate. Ctrl-C to end.
     test_signal-10213 [020] 51151620.771581: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=10213 grp=1 res=0
     test_signal-10213 [020] 51151620.771582: <stack trace>
 => send_signal
 => __group_send_sig_info
 => check_cpu_itimer
 => run_posix_cpu_timers
 => update_process_times
 => tick_sched_timer
 => __run_hrtimer
 => hrtimer_interrupt
```

## Reference

* [Executable and Linkable Format](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)
* [Executable and Linking Format (ELF) Specification](http://refspecs.linuxbase.org/elf/elf.pdf)
* [Anatomy of an ELF core file](http://www.gabriel.urdhr.fr/2015/05/29/core-file/)
* [A brief look into core dumps](http://uhlo.blogspot.fr/2012/05/brief-look-into-core-dumps.html)
* [The high-resolution timer API](https://lwn.net/Articles/167897/)
* [浅析 Linux 中的时间编程和实现原理，第 4 部分: Linux 内核的工作](https://www.ibm.com/developerworks/cn/linux/1308_liuming_linuxtime4/)