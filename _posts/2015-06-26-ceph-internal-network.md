---
layout: post
title: Ceph源码解析：网络模块
date: 2015-06-26 12:00:30
categories: Linux
tags: ceph
excerpt: Ceph源码解析：网络模块。
---
由于Ceph的历史很久，网络没有采用现在常用的事件驱动（epoll）的模型，而是采用了与MySQL类似的多线程模型，每个连接(socket)有一个读线程，不断从socket读取，一个写线程，负责将数据写到socket。多线程实现简单，但并发性能就不敢恭维了。

Messenger是网络模块的核心数据结构，负责接收/发送消息。OSD主要有两个Messenger：ms_public处于与客户端的消息，ms_cluster处理与其它OSD的消息。

# 数据结构

![](/assets/2015-06-26-ceph-internal-network-3.png) 

网络模块的核心是SimpleMessager：

(1)它包含一个Accepter对象，它会创建一个单独的线程，用于接收新的连接（Pipe）。

```c
void *Accepter::entry()
{
...
    int sd = ::accept(listen_sd, (sockaddr*)&addr.ss_addr(), &slen);
    if (sd >= 0) {
      errors = 0;
      ldout(msgr->cct,10) << "accepted incoming on sd " << sd << dendl;
      
      msgr->add_accept_pipe(sd);
...

//创建新的Pipe
Pipe *SimpleMessenger::add_accept_pipe(int sd)
{
  lock.Lock();
  Pipe *p = new Pipe(this, Pipe::STATE_ACCEPTING, NULL);
  p->sd = sd;
  p->pipe_lock.Lock();
  p->start_reader();
  p->pipe_lock.Unlock();
  pipes.insert(p);
  accepting_pipes.insert(p);
  lock.Unlock();
  return p;
}
```

(2)包含所有的连接对象(Pipe)，每个连接Pipe有一个读线程/写线程。读线程负责从socket读取数据，然后放消息放到DispatchQueue分发队列。写线程负责从发送队列取出Message，然后写到socket。

```c
  class Pipe : public RefCountedObject {
    /**
     * The Reader thread handles all reads off the socket -- not just
     * Messages, but also acks and other protocol bits (excepting startup,
     * when the Writer does a couple of reads).
     * All the work is implemented in Pipe itself, of course.
     */
    class Reader : public Thread {
      Pipe *pipe;
    public:
      Reader(Pipe *p) : pipe(p) {}
      void *entry() { pipe->reader(); return 0; }
    } reader_thread;  ///读线程
    friend class Reader;

    /**
     * The Writer thread handles all writes to the socket (after startup).
     * All the work is implemented in Pipe itself, of course.
     */
    class Writer : public Thread {
      Pipe *pipe;
    public:
      Writer(Pipe *p) : pipe(p) {}
      void *entry() { pipe->writer(); return 0; }
    } writer_thread; ///写线程
    friend class Writer;

...
    ///发送队列
    map<int, list<Message*> > out_q;  // priority queue for outbound msgs
    DispatchQueue *in_q;  ///接收队列
```

(3)包含一个分发队列DispatchQueue，分发队列有一个专门的分发线程（DispatchThread），将消息分发给Dispatcher（OSD）完成具体逻辑处理。

# 消息的接收

接收流程如下：
![](/assets/2015-06-26-ceph-internal-network-1.jpg) 
Pipe的读线程从socket读取Message，然后放入接收队列，再由分发线程取出Message交给Dispatcher处理。

# 消息的发送

发送流程如下：
![](/assets/2015-06-26-ceph-internal-network-2.jpg) 

# 其它资料

这篇文章[解析Ceph: 网络层的处理](http://www.wzxue.com/ceph-network/)简单介绍了一下Ceph的网络，但对Pipe与Connection的关系描述似乎不太准确，Pipe是对socket的封装，Connection更加上层、抽象。

