---
layout: post
title: cgroup memory usage statistic
date: 2015-11-30 18:16:30
categories: Linux
tags: memory cgroup
excerpt: cgroup memory usage statistic
---

之前写过一篇cgroup内存统计的[文章](http://hustcat.github.io/memory-usage-in-process-and-cgroup/)，这里再做一些补充。

几个内存统计相关的变量：

* cache       - # of bytes of page cache memory.

	file cache，包括shm page

* rss     - # of bytes of anonymous and swap cache memory (includes transparent hugepages).

    包括所有的anon page和swap cache page

* mapped_file - # of bytes of mapped file (includes tmpfs/shmem)

	当前页表正在映射文件的page(包括shm page)

* inactive_anon   - # of bytes of anonymous and swap cache memory on inactive LRU list.

    inactive LRU，包括anon page和swap cache page

* active_anon - # of bytes of anonymous and swap cache memory on active LRU list.

* inactive_file   - # of bytes of file-backed memory on inactive LRU list.

    inactive LRU，file page

* active_file - # of bytes of file-backed memory on active LRU list.

从统计的角度，内核将page分成四类：

(1)anon page：比如malloc分配的page。这类page会影响rss、inactive_anon(active_anon)

(2)file mapped page：文件映射的page，比如通过mmap映射文件。这类page会影响cache、inactive_file(active_file)

(3)shm page：影响cache、inactive_anon(active_anon)

(4)swap cache page：影响rss、inactive_anon(active_anon)

实际上：

```
cache: file cache page + shm page
rss:  anonymous page + swap cache page
(in)active_anon: anonymous page + swap cache page + shm page
(in)active_file: file cache page
```

rss + cache = (in)active_anon + (in)active_file


# anon page

![](/assets/2015-11-30-cgroup-memory-statistic-again-1.jpg)

从上面可以看出，当发生缺页时，如果是anon page，内核会分配一个page，影响rss和active_anon。

示例：

```
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define BUF_SIZE 4000000000   

int main(int argc,char **argv){
    char *shmptr = (char*) malloc(BUF_SIZE);
    memset(shmptr,'\0',1000000000);

    printf("sleep...\n");
    while(1)
        sleep(1);

    exit(0);
}
```

测试前

```
cache 59232256
rss 7950336
mapped_file 8232960
inactive_anon 2633728
active_anon 7958528
inactive_file 37187584
active_file 19402752
```

测试后

```
cache 59232256
rss 1007697920
mapped_file 8216576
inactive_anon 2633728
active_anon 1007706112
inactive_file 37158912
active_file 19431424
```
可以看到，rss和active_anon发生了变化。

# file mapped page(include shm)

![](/assets/2015-11-30-cgroup-memory-statistic-again-2.jpg)

当发生缺页时，如果page是映射文件的page，会分配page，并从磁盘(tmpfs)读取对应的数据，影响cache、(in)active_file。严格来说，shm没有磁盘文件，但shm建立在tmpfs之上，所以，内核也将其与文件同等对待，但page会加到(in)active_anon LRU list。

示例：

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define BUF_SIZE 4000000000   
#define MYKEY 26 

int main(int argc,char **argv){
    int shmid;
    char *shmptr;

    if((shmid = shmget(MYKEY,BUF_SIZE,IPC_CREAT)) ==-1){
        fprintf(stderr,"Create Share Memory Error0m~Z%s\n\a",strerror(errno));
        exit(1);
    }

    if((shmptr =shmat(shmid,0,0))==(void *)-1){
        printf("shmat error!\n");
        exit(1);
    }

    memset(shmptr,'\0',1000000000);

    printf("sleep...\n");
    while(1)
        sleep(1);

    exit(0);
}
```

程序运行前：

```
cache 65875968
rss 7643136
mapped_file 8273920
inactive_anon 2633728
active_anon 7651328
inactive_file 43126784
active_file 20107264
```

程序运行中：

```
cache 1065881600
rss 7729152
mapped_file 1008279552
inactive_anon 1002635264
active_anon 7737344
inactive_file 43118592
active_file 20119552
```

程序运行后：

```
cache 1066213376
rss 7852032
mapped_file 8404992
inactive_anon 1002635264
active_anon 7860224
inactive_file 43356160
active_file 20213760
```

可以看到，共享内存的分配影响cache、inactive_anon和mapped_file。但值得注意的是，当程序退出之后，mapped_file减小，但inactive_anon并没有变。

实际上，mapped_file表示当前内存页表当前正在映射的page，由于进程退出，页表映射取消，但page并没有free或者swap out，所以inactive_anon不变。

# swap cache page

![](/assets/2015-11-30-cgroup-memory-statistic-again-3.jpg)

当发生缺页时，如果page对应交换分区，会分配page，并从swap读取数据，影响rss和(in)active_anon。

另外，注意swap cache与(file)cache的区别，前者用于anon page换出时保存数据，后者指file cache page(include shm)。

