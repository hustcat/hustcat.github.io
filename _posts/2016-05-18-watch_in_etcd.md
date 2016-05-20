---
layout: post
title: Watch API and TTL implementation in etcd
date: 2016-05-18 17:00:30
categories: Distributed
tags: etcd
excerpt: Watch API and TTL implementation in etcd
---

## Watch API

[Etcd Watch API](https://github.com/coreos/etcd/blob/v0.4.6/Documentation/api.md#waiting-for-a-change)可以用于当Client监听数据（Key）的变化，从而得到实时通知。

* notify

当etcd更新一个key时，就会将event暂存，并通知所有正在watch的Watcher(即client)：

```go
// Set creates or replace the node at nodePath.
func (s *store) Set(nodePath string, dir bool, value string, expireTime time.Time) (*Event, error) {
...
	// Set new value
	e, err := s.internalCreate(nodePath, dir, value, false, true, expireTime, Set)
...
	s.WatcherHub.notify(e) ///save event

	return e, nil
}


// notify function accepts an event and notify to the watchers.
func (wh *watcherHub) notify(e *Event) {
	e = wh.EventHistory.addEvent(e) // add event into the eventHistory

	segments := strings.Split(e.Node.Key, "/")

	currPath := "/"

	// walk through all the segments of the path and notify the watchers
	// if the path is "/foo/bar", it will notify watchers with path "/",
	// "/foo" and "/foo/bar"

	for _, segment := range segments {
		currPath = path.Join(currPath, segment)
		// notify the watchers who interests in the changes of current path
		wh.notifyWatchers(e, currPath, false)
	}
}
```

* watching

```go
func handleWatch(key string, recursive, stream bool, waitIndex string, w http.ResponseWriter, req *http.Request, s Server) error {
	// Create a command to watch from a given index (default 0).
	var sinceIndex uint64 = 0
	var err error

	if waitIndex != "" {
		sinceIndex, err = strconv.ParseUint(waitIndex, 10, 64)
	}

	watcher, err := s.Store().Watch(key, recursive, stream, sinceIndex)

...
	select {
	case <-closeChan:
		watcher.Remove()
	case event := <-watcher.EventChan: ///等待Update事件
		if req.Method == "HEAD" {
			return nil
		}
		b, _ := json.Marshal(event)
		w.Write(b)
	}
}
```

Client还可以指定Index，watch历史数据：

```sh
# curl -s -L 'http://127.0.0.1:4001/v2/keys/dir1/d1/key1' | python -mjson.tool                          
{
    "action": "get",
    "node": {
        "createdIndex": 20024,
        "key": "/dir1/d1/key1",
        "modifiedIndex": 20024,
        "value": "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy"
    }
}

### watch index=20021后，key1的value:

# curl -s -L 'http://127.0.0.1:4001/v2/keys/dir1/d1/key1?wait=true&waitIndex=20021' | python -mjson.tool
{
    "action": "set",
    "node": {
        "createdIndex": 20023,
        "key": "/dir1/d1/key1",
        "modifiedIndex": 20023,
        "value": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    },
    "prevNode": {
        "createdIndex": 20020,
        "key": "/dir1/d1/key1",
        "modifiedIndex": 20020,
        "value": "value1"
    }
}
```

不过，etcd的watch的历史数据最多1000条，即etcd只会保存1000个index的历史event：

```go
func newStore() *store {
	s := new(store)
	s.CurrentVersion = defaultVersion
	s.Root = newDir(s, "/", s.CurrentIndex, nil, "", Permanent)
	s.Stats = newStats()
	s.WatcherHub = newWatchHub(1000) ///1000 history event
	s.ttlKeyHeap = newTtlKeyHeap()
	return s
}
```

## TTL

* ttlKeyHeap

对于设置有TTL的Key/Value，etcd内部会用一个heap来记录相应的node：

```go
/// implement raft.StateMachine
type store struct {
	Root           *node ///root node
	WatcherHub     *watcherHub
	CurrentIndex   uint64
	Stats          *Stats
	CurrentVersion int
	ttlKeyHeap     *ttlKeyHeap  // need to recovery manually
	worldLock      sync.RWMutex // stop the world lock
}

func (s *store) internalCreate(nodePath string, dir bool, value string, unique, replace bool,
	expireTime time.Time, action string) (*Event, error) {
...
	// node with TTL
	if !n.IsPermanent() {
		s.ttlKeyHeap.push(n)

		eNode.Expiration, eNode.TTL = n.ExpirationAndTTL()
	}

	s.CurrentIndex = nextIndex
}
```

* SyncCommand

leader每隔500ms就会执行一次SyncCommand命令，删除过期的node：

```go
func (s *PeerServer) monitorSync() {
	ticker := time.NewTicker(time.Millisecond * 500)
	defer ticker.Stop()
	for {
		select {
		case <-s.closeChan:
			return
		case now := <-ticker.C:
			if s.raftServer.State() == raft.Leader {
				s.raftServer.Do(s.store.CommandFactory().CreateSyncCommand(now)) ///执行SyncCommand命令
			}
		}
	}
}

func (c SyncCommand) Apply(context raft.Context) (interface{}, error) {
	s, _ := context.Server().StateMachine().(store.Store)
	s.DeleteExpiredKeys(c.Time)

	return nil, nil
}

// deleteExpiredKyes will delete all
func (s *store) DeleteExpiredKeys(cutoff time.Time) {
	s.worldLock.Lock()
	defer s.worldLock.Unlock()

	for {
		node := s.ttlKeyHeap.top()
		if node == nil || node.ExpireTime.After(cutoff) { ///node过期
			break
		}
...
```