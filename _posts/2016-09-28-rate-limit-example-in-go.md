---
layout: post
title: Rate limit examples in Go
date: 2016-09-28 16:00:30
categories: 编程语言
tags: go
excerpt: Rate limit examples in Go
---

## Example1: by tickers

```go
// _[Rate limiting](http://en.wikipedia.org/wiki/Rate_limiting)_
// is an important mechanism for controlling resource
// utilization and maintaining quality of service. Go
// elegantly supports rate limiting with goroutines,
// channels, and [tickers](tickers).

package main

import "time"
import "fmt"

func main() {

    // First we'll look at basic rate limiting. Suppose
    // we want to limit our handling of incoming requests.
    // We'll serve these requests off a channel of the
    // same name.
    requests := make(chan int, 5)
    for i := 1; i <= 5; i++ {
        requests <- i
    }
    close(requests)

    // This `limiter` channel will receive a value
    // every 200 milliseconds. This is the regulator in
    // our rate limiting scheme.
    limiter := time.Tick(time.Millisecond * 200)

    // By blocking on a receive from the `limiter` channel
    // before serving each request, we limit ourselves to
    // 1 request every 200 milliseconds.
    for req := range requests {
        <-limiter
        fmt.Println("request", req, time.Now())
    }

    // We may want to allow short bursts of requests in
    // our rate limiting scheme while preserving the
    // overall rate limit. We can accomplish this by
    // buffering our limiter channel. This `burstyLimiter`
    // channel will allow bursts of up to 3 events.
    burstyLimiter := make(chan time.Time, 3)

    // Fill up the channel to represent allowed bursting.
    for i := 0; i < 3; i++ {
        burstyLimiter <- time.Now()
    }

    // Every 200 milliseconds we'll try to add a new
    // value to `burstyLimiter`, up to its limit of 3.
    go func() {
        for t := range time.Tick(time.Millisecond * 200) {
            burstyLimiter <- t
        }
    }()

    // Now simulate 5 more incoming requests. The first
    // 3 of these will benefit from the burst capability
    // of `burstyLimiter`.
    burstyRequests := make(chan int, 5)
    for i := 1; i <= 5; i++ {
        burstyRequests <- i
    }
    close(burstyRequests)
    for req := range burstyRequests {
        <-burstyLimiter
        fmt.Println("request", req, time.Now())
    }
}
```

More see [Go by Example: Rate Limiting](https://gobyexample.com/rate-limiting).


## Example2: [juju/ratelimit](https://github.com/juju/ratelimit)

```go
package main

import (
    "bytes"
    "fmt"
    "io"
    "time"

    "github.com/juju/ratelimit"
)

func main() {
    // Source holding 1MB
    src := bytes.NewReader(make([]byte, 1024*1024))
    // Destination
    dst := &bytes.Buffer{}

    // Bucket adding 100KB every second, holding max 100KB
    bucket := ratelimit.NewBucketWithRate(100*1024, 100*1024)

    start := time.Now()

    // Copy source to destination, but wrap our reader with rate limited one
    io.Copy(dst, ratelimit.Reader(src, bucket))

    fmt.Printf("Copied %d bytes in %s\n", dst.Len(), time.Since(start))
}
```

```
# go run test2.go 
Copied 1048576 bytes in 9.239732929s
```


* ratelimit.Reader

```go
type reader struct {
	r      io.Reader
	bucket *Bucket
}

// Reader returns a reader that is rate limited by
// the given token bucket. Each token in the bucket
// represents one byte.
func Reader(r io.Reader, bucket *Bucket) io.Reader {
	return &reader{
		r:      r,
		bucket: bucket,
	}
}

func (r *reader) Read(buf []byte) (int, error) {
	n, err := r.r.Read(buf)
	if n <= 0 {
		return n, err
	}
	r.bucket.Wait(int64(n))
	return n, err
}

```

More refer [here](http://stackoverflow.com/questions/27187617/how-would-i-limit-upload-and-download-speed-from-the-server-in-golang).


## Example3: [time/rate](https://github.com/golang/time/rate)

```go
package main

import (
    "bytes"
    "fmt"
    "io"
    "time"

    "golang.org/x/time/rate"
)

type reader struct {
    r      io.Reader
    limiter *rate.Limiter
}

// Reader returns a reader that is rate limited by
// the given token bucket. Each token in the bucket
// represents one byte.
func NewReader(r io.Reader, l *rate.Limiter) io.Reader {
    return &reader{
        r:      r,
        limiter:l,
    }
}

func (r *reader) Read(buf []byte) (int, error) {
    n, err := r.r.Read(buf)
    if n <= 0 {
        return n, err
    }

    now := time.Now()
    rv := r.limiter.ReserveN(now, n)
    if !rv.OK() {
        return 0, fmt.Errorf("Exceeds limiter's burst")
    }
    delay := rv.DelayFrom(now)
    //fmt.Printf("Read %d bytes, delay %d\n", n, delay)
    time.Sleep(delay)
    return n, err
}

func main() {
    // Source holding 1MB
    src := bytes.NewReader(make([]byte, 1024*1024))
    // Destination
    dst := &bytes.Buffer{}

    // Bucket adding 100KB every second, holding max 100KB
    limit := rate.NewLimiter(100*1024, 100*1024)

    start := time.Now()

    buf := make([]byte, 10*1024)
    // Copy source to destination, but wrap our reader with rate limited one
    //io.CopyBuffer(dst, NewReader(src, limit), buf)
    r := NewReader(src, limit)
    for{
        if n, err := r.Read(buf); err == nil {
            dst.Write(buf[0:n])
        }else{
            break
        }
    }

    fmt.Printf("Copied %d bytes in %s\n", dst.Len(), time.Since(start))
}
```

```
$ go run test3.go 
Copied 1048576 bytes in 9.241212473s
```

值得注意的是，这里不能直接用`io.Copy`，`bytes.Buffer`实现了ReaderFrom，每次Read的时候，buf的长度是变化的，会导致len(buf)超过`rate.Limiter.burst`。对于这种情况，`rv.DelayFrom(now)`会返回[InfDuration](https://github.com/golang/time/blob/master/rate/rate.go#L136)。


```
Breakpoint 2, main.(*reader).Read (r=0x82038e000, buf= []uint8 = {...}, ~r1=34899238912, ~r2=...)
    at /Users/yy/dev/go/src/github.com/hustcat/golangexample/ratelimit/test3.go:28
28      fmt.Printf("buf len=%d ", len(buf))
(gdb) bt
#0  main.(*reader).Read (r=0x82038e000, buf= []uint8 = {...}, ~r1=34899238912, ~r2=...)
    at /Users/yy/dev/go/src/github.com/hustcat/golangexample/ratelimit/test3.go:28
#1  0x000000000005a50f in bytes.(*Buffer).ReadFrom (b=0x820384000, r=..., n=0, err=...)
    at /usr/local/go/src/bytes/buffer.go:173
#2  0x00000000000728b0 in io.copyBuffer (dst=..., src=..., buf= []uint8 = {...}, written=0, err=...)
    at /usr/local/go/src/io/io.go:375
#3  0x00000000000726f1 in io.CopyBuffer (dst=..., src=..., buf= []uint8 = {...}, written=1003680, err=...)
    at /usr/local/go/src/io/io.go:362
#4  0x0000000000002925 in main.main ()
    at /Users/yy/dev/go/src/github.com/hustcat/golangexample/ratelimit/test3.go:58
```
