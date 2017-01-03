---
layout: post
title: Timeout in Go net/http client
date: 2016-12-30 17:00:30
categories: 编程语言
tags: golang
excerpt: Timeout in Go net/http client
---

使用`net/http client`的一般流程如下:

```go
tr := &http.Transport{
	TLSClientConfig:    &tls.Config{RootCAs: pool},
	DisableCompression: true,
}

client := &http.Client{
	Transport: tr,
	CheckRedirect: redirectPolicyFunc,
}

req, err := http.NewRequest("GET", "http://example.com", nil)
// ...
req.Header.Add("If-None-Match", `W/"wyzzy"`)
resp, err := client.Do(req)
// ...
```

`http.Client.Do`的实现:

![](/assets/golang/net-http-timeout-client-1.jpg)

## 数据结构

* Transport

`Transport`代表client与server之间的传输管道，它比`net.Conn`更加高层，它基于`net.Conn`(实际上是`http.persistConn`)进行数据传输，并管理空闲的`net.Conn`.

```go
// Transport is an implementation of RoundTripper that supports http,
// https, and http proxies (for either http or https with CONNECT).
// Transport can also cache connections for future re-use.
type Transport struct {
	idleConn    map[connectMethodKey][]*persistConn ///空闲的连接对象
	reqCanceler map[*Request]func() ///实现Cancel request

	// Dial specifies the dial function for creating TCP
	// connections.
	// If Dial is nil, net.Dial is used.
	Dial func(network, addr string) (net.Conn, error)

	// TLSHandshakeTimeout specifies the maximum amount of time waiting to
	// wait for a TLS handshake. Zero means no timeout.
	TLSHandshakeTimeout time.Duration

	// ResponseHeaderTimeout, if non-zero, specifies the amount of
	// time to wait for a server's response headers after fully
	// writing the request (including its body, if any). This
	// time does not include the time to read the response body.
	ResponseHeaderTimeout time.Duration
}


var DefaultTransport RoundTripper = &Transport{
	Proxy: ProxyFromEnvironment,
	Dial: (&net.Dialer{
		Timeout:   30 * time.Second,
		KeepAlive: 30 * time.Second,
	}).Dial,
	TLSHandshakeTimeout: 10 * time.Second,
}
```


`net.Dialer.Timeout` limits the time spent establishing a TCP connection (if a new one is needed).
`http.Transport.TLSHandshakeTimeout` limits the time spent performing the TLS handshake.
`http.Transport.ResponseHeaderTimeout` limits the time spent reading the headers of the response.


* Client

```go
// A Client is higher-level than a RoundTripper (such as Transport)
// and additionally handles HTTP details such as cookies and
// redirects.
type Client struct {
	// Transport specifies the mechanism by which individual
	// HTTP requests are made.
	// If nil, DefaultTransport is used.
	Transport RoundTripper

	// Timeout specifies a time limit for requests made by this
	// Client. The timeout includes connection time, any
	// redirects, and reading the response body. The timer remains
	// running after Get, Head, Post, or Do return and will
	// interrupt reading of the Response.Body.
	//
	// A Timeout of zero means no timeout.
	//
	// The Client's Transport must support the CancelRequest
	// method or Client will return errors when attempting to make
	// a request with Get, Head, Post, or Do. Client's default
	// Transport (DefaultTransport) supports CancelRequest.
	Timeout time.Duration
}
```

`Client.Timeout` specifies a time limit for requests made by this Client. The timeout includes connection time, any redirects, and reading the response body.

`Client`要求底层的`Transport`支持`CancelRequest`方法:

```go
// CancelRequest cancels an in-flight request by closing its
// connection.
func (t *Transport) CancelRequest(req *Request) {
	t.reqMu.Lock()
	cancel := t.reqCanceler[req]
	t.reqMu.Unlock()
	if cancel != nil {
		cancel()
	}
}
```


几个timeout变量的关系:

![](/assets/golang/net-http-timeout-client.png)

## 实现

* Client.Timeout

```go
func (c *Client) Do(req *Request) (resp *Response, err error) {
	if req.Method == "GET" || req.Method == "HEAD" {
		return c.doFollowingRedirects(req, shouldRedirectGet)
	}
	if req.Method == "POST" || req.Method == "PUT" {
		return c.doFollowingRedirects(req, shouldRedirectPost)
	}
	return c.send(req)
}

func (c *Client) doFollowingRedirects(ireq *Request, shouldRedirect func(int) bool) (resp *Response, err error) {
///...
	var timer *time.Timer
	if c.Timeout > 0 {
		type canceler interface {
			CancelRequest(*Request)
		}
		tr, ok := c.transport().(canceler)
		if !ok {
			return nil, fmt.Errorf("net/http: Client Transport of type %T doesn't support CancelRequest; Timeout not supported", c.transport())
		}
		timer = time.AfterFunc(c.Timeout, func() {
			reqmu.Lock()
			defer reqmu.Unlock()
			tr.CancelRequest(req) ///超时，则取消request
		})
	}
```

* Transport.TLSHandshakeTimeout

```go
func (t *Transport) dialConn(cm connectMethod) (*persistConn, error) {
	conn, err := t.dial("tcp", cm.addr()) ///tcp conn

	pconn := &persistConn{
		t:          t,
		cacheKey:   cm.key(),
		conn:       conn,
		reqch:      make(chan requestAndChan, 1),
		writech:    make(chan writeRequest, 1),
		closech:    make(chan struct{}),
		writeErrCh: make(chan error, 1),
	}

///...
	if cm.targetScheme == "https" {

		plainConn := conn
		tlsConn := tls.Client(plainConn, cfg)
		errc := make(chan error, 2)
		var timer *time.Timer // for canceling TLS handshake
		if d := t.TLSHandshakeTimeout; d != 0 {
			timer = time.AfterFunc(d, func() {
				errc <- tlsHandshakeTimeoutError{}  ///超时
			})
		}
		go func() {
			err := tlsConn.Handshake()
			if timer != nil {
				timer.Stop()
			}
			errc <- err
		}()
		if err := <-errc; err != nil {
			plainConn.Close()
			return nil, err
		}

}
```

* Transport.ResponseHeaderTimeout

```go
func (pc *persistConn) roundTrip(req *transportRequest) (resp *Response, err error) {
	pc.t.setReqCanceler(req.Request, pc.cancelRequest)

	// Write the request concurrently with waiting for a response,
	// in case the server decides to reply before reading our full
	// request body.
	writeErrCh := make(chan error, 1)
	pc.writech <- writeRequest{req, writeErrCh} ///persistConn.writeLoop write Request

	resc := make(chan responseAndError, 1)
	pc.reqch <- requestAndChan{req.Request, resc, requestedGzip} ///persistConn.readLoop read Response

	var re responseAndError
	var pconnDeadCh = pc.closech
	var failTicker <-chan time.Time
	var respHeaderTimer <-chan time.Time
WaitResponse:
	for {
		select {
		case err := <-writeErrCh:
			if err != nil {
				re = responseAndError{nil, err}
				pc.close()
				break WaitResponse
			}
			if d := pc.t.ResponseHeaderTimeout; d > 0 { ///发送请求成功,然后设置ResponseHeaderTimeout
				respHeaderTimer = time.After(d)
			}
		case <-pconnDeadCh:
			// The persist connection is dead. This shouldn't
			// usually happen (only with Connection: close responses
			// with no response bodies), but if it does happen it
			// means either a) the remote server hung up on us
			// prematurely, or b) the readLoop sent us a response &
			// closed its closech at roughly the same time, and we
			// selected this case first, in which case a response
			// might still be coming soon.
			//
			// We can't avoid the select race in b) by using a unbuffered
			// resc channel instead, because then goroutines can
			// leak if we exit due to other errors.
			pconnDeadCh = nil                               // avoid spinning
			failTicker = time.After(100 * time.Millisecond) // arbitrary time to wait for resc
		case <-failTicker:
			re = responseAndError{err: errClosed}
			break WaitResponse
		case <-respHeaderTimer:  ///ResponseHeaderTimeout
			pc.close()
			re = responseAndError{err: errTimeout}
			break WaitResponse
		case re = <-resc:
			break WaitResponse
		}
	}

```

## Docker pull client

Docker在pull镜像时，设置了`net.Dialer.Timeout`:

```go
///registry/registry.go
func newClient(jar http.CookieJar, roots *x509.CertPool, cert *tls.Certificate, timeout TimeoutType, secure bool) *http.Client {

	httpTransport := &http.Transport{
		DisableKeepAlives: true,
		Proxy:             http.ProxyFromEnvironment,
		TLSClientConfig:   &tlsConfig,
	}

	switch timeout {
	case ConnectTimeout:
		httpTransport.Dial = func(proto string, addr string) (net.Conn, error) {
			// Set the connect timeout to 5 seconds
			conn, err := net.DialTimeout(proto, addr, 5*time.Second)
			if err != nil {
				return nil, err
			}
			// Set the recv timeout to 10 seconds
			conn.SetDeadline(time.Now().Add(10 * time.Second))
			return conn, nil
		}
	case ReceiveTimeout: ///go here
		httpTransport.Dial = func(proto string, addr string) (net.Conn, error) {
			conn, err := net.Dial(proto, addr)
			if err != nil {
				return nil, err
			}
			conn = utils.NewTimeoutConn(conn, 1*time.Minute)
			return conn, nil
		}
	}

	return &http.Client{
		Transport:     httpTransport,
		CheckRedirect: AddRequiredHeadersToRedirectedRequests,
		Jar:           jar,
	}
}
```

同时，还调用了`net.Conn.SetReadDeadline`设置了read的超时时间：

```go
func NewTimeoutConn(conn net.Conn, timeout time.Duration) net.Conn {
	return &TimeoutConn{conn, timeout}
}

// A net.Conn that sets a deadline for every Read or Write operation
type TimeoutConn struct {
	net.Conn
	timeout time.Duration
}

func (c *TimeoutConn) Read(b []byte) (int, error) {
	if c.timeout > 0 {
		err := c.Conn.SetReadDeadline(time.Now().Add(c.timeout))
		if err != nil {
			return 0, err
		}
	}
	return c.Conn.Read(b)
}
```


* net.Conn.SetReadDeadline

`SetReadDeadline`用于实现Go的网络IO超时原语，它会给`netFD`创建对应的IO定时器，当定时器超时，runtime会调用`runtime·ready`唤醒对应进行`Read/Write`的goroutine，如果对应的goroutine处于等待的状态(默认情况下deadline为0，不会创建定时器)。

Go并没有使用`epoll_wait`实现IO的超时，而是通过`Set[Read|Write]Deadline(time.Time)`对每个`netFD`设置超时。

当`SetDeadline`设置的定时器超时后，在超时处理函数中，会删除该定时器；而且，每次收到或者发送数据时，也不会reset该定时器。所以，每次`Read/Write`操作之前，都需要调用该函数。参考[The Go netpoller and timeout](/go-netpoller-and-timeout).


```go
//net/net.go
// SetReadDeadline implements the Conn SetReadDeadline method.
func (c *conn) SetReadDeadline(t time.Time) error {
	if !c.ok() {
		return syscall.EINVAL
	}
	return c.fd.setReadDeadline(t)
}


//net/net_poll_runtime.go
func (fd *netFD) setReadDeadline(t time.Time) error {
	return setDeadlineImpl(fd, t, 'r')
}

func (fd *netFD) setWriteDeadline(t time.Time) error {
	return setDeadlineImpl(fd, t, 'w')
}

func setDeadlineImpl(fd *netFD, t time.Time, mode int) error {
	d := runtimeNano() + int64(t.Sub(time.Now()))
	if t.IsZero() {
		d = 0
	}
	if err := fd.incref(); err != nil {
		return err
	}
	runtime_pollSetDeadline(fd.pd.runtimeCtx, d, mode)
	fd.decref()
	return nil
}
```

## Reference

* [The complete guide to Go net/http timeouts](https://blog.cloudflare.com/the-complete-guide-to-golang-net-http-timeouts/)