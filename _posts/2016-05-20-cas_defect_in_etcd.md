---
layout: post
title: Dive into the defect of Etcd-Index and Raft-Index in etcd 
date: 2016-05-20 17:00:30
categories: Distributed
tags: etcd
excerpt: Dive into the defect of Etcd-Index and Raft-Index in etcd
---

Etcd是一个基于raft算法实现的强一致的分布式KV存储系统，当然，所谓强一致，只是建立在对内部实现原理和代码完全掌握的基础上。

*** 注意：下面的讨论基于etcd V0.4.x ***

## Index

etcd会在Http response header附带下面几个Index信息：

```
X-Etcd-Index: 35
X-Raft-Index: 5398
X-Raft-Term: 0
```
*** etcd为什么要搞2个Index? ***

X-Etcd-Index Etcd内部的一个全局全变量，表示etcd server逻辑上处理了多少个（带自client的）更新请求，比如处理一个Set操作，就会加1。

X-Raft-Index 对应go-raft内部的一个全局变量log.commitIndex，表示内部raft协议（算法）节间之点同步了多个log entry，leader节点在创建log entry时，会计算log.nextIndex，然后赋值给log entry。注意，并不是只有上层etcd server的更新操作会导致raft协议创建log entry。

一般来说，如果各个节点的X-Raft-Index值相同，则X-Etcd-Index也应该相同。但实际上却未必。X-Raft-Index值相同，说明log entry已经同步到各个节点，但是log entry却未必能在follower重放(apply)成功。

一般来说，Create/Set/Delete/Update这些简单操作，如果能在leader成功，那么在follower也应该能够成功执行。但是，对于一些稍微高级的操作，比如CAS(Atomic Compare-and-Swap)，却未必如此，即使X-Raft-Index完全相同。

## CAS in etcd

Etcd提供了一个原子更新操作API-[CAS](https://github.com/coreos/etcd/blob/v0.4.6/Documentation/api.md#atomic-compare-and-swap)。该接口的语义是在修改一个Key对应的Value的时候，会比较Key的Value与参数中的Value（prevValue）和Index（prevIndex）是否相同；

```go
func (s *store) CompareAndSwap(nodePath string, prevValue string, prevIndex uint64,
	value string, expireTime time.Time) (*Event, error) {
...
	// If both of the prevValue and prevIndex are given, we will test both of them.
	// Command will be executed, only if both of the tests are successful.
	if ok, which := n.Compare(prevValue, prevIndex); !ok {
		cause := getCompareFailCause(n, which, prevValue, prevIndex)
		s.Stats.Inc(CompareAndSwapFail)
		return nil, etcdErr.NewError(etcdErr.EcodeTestFailed, cause, s.CurrentIndex)
	}

	// update etcd index
	s.CurrentIndex++
...
}
```

如果不同，说明在prevValue之后，当前更新之前这段时间发生过其它更新，则etcd会放弃本次更新，并将统计参数CompareAndSwapFail加1；如果相同，说明这之间没有其它更新，则更新Value。

假设某一时刻，某个节点异常重启，Etcd-Index变得不一致，从而导致后面新创建的Key/Value的createIndex在leader与follower节点会不一致。这之后，所有在leader的执行的CAS操作，都不能在follower节点成功执行。而内部的raft算法却不能检测到这种错误，leader的log entry仍然发到follower节点，并且在leader与follower成功commit。

etcd上层的一致性检查机制（Etcd-Index）与底层的一致性机制(raft协议)的脱节，可能会严重引起数据层面的逻辑不一致，如果业务层面不做一致性监控（etcd的一致性和业务数据本身的一致性检查），可能会造成严重后果。

所以，V2.x开如，etcd不再使用go-raft，而是自己完全实现raft算法。有待进一步确认代码。

## 结束语

总的来说，raft算法很简单优美、etcd的实现也很简单，但是一个分布式的存储系统的成熟，不是在短时间内能一促而成的。
