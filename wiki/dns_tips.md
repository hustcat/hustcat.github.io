`getaddrinfo`默认使用UDP访问`DNS SERVER`，并依次使用`/etc/resolv.conf`中的`search`行的项：

```
...
socket(PF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_IP) = 4
connect(4, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, 16) = 0
poll([{fd=4, events=POLLOUT}], 1, 0)    = 1 ([{fd=4, revents=POLLOUT}])
sendto(4, "\332C\1\0\0\1\0\0\0\0\0\0\7nginx-1\5dbyin\3svc\7c"..., 49, MSG_NOSIGNAL, NULL, 0) = 49
poll([{fd=4, events=POLLIN}], 1, 1000)  = 1 ([{fd=4, revents=POLLIN}])
ioctl(4, FIONREAD, [141])               = 0
recvfrom(4, "\332C\201\3\0\1\0\0\0\1\0\0\7nginx-1\5dbyin\3svc\7c"..., 1024, 0, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, [16]) = 141
close(4)                                = 0
socket(PF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_IP) = 4
connect(4, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, 16) = 0
poll([{fd=4, events=POLLOUT}], 1, 0)    = 1 ([{fd=4, revents=POLLOUT}])
sendto(4, "\36^\1\0\0\1\0\0\0\0\0\0\7nginx-1\3svc\7cluster"..., 43, MSG_NOSIGNAL, NULL, 0) = 43
poll([{fd=4, events=POLLIN}], 1, 1000)  = 1 ([{fd=4, revents=POLLIN}])
ioctl(4, FIONREAD, [135])               = 0
recvfrom(4, "\36^\201\3\0\1\0\0\0\1\0\0\7nginx-1\3svc\7cluster"..., 1024, 0, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, [16]) = 135
close(4)                                = 0
socket(PF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_IP) = 4
connect(4, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, 16) = 0
poll([{fd=4, events=POLLOUT}], 1, 0)    = 1 ([{fd=4, revents=POLLOUT}])
sendto(4, "}\210\1\0\0\1\0\0\0\0\0\0\7nginx-1\7cluster\5loc"..., 39, MSG_NOSIGNAL, NULL, 0) = 39
poll([{fd=4, events=POLLIN}], 1, 1000)  = 1 ([{fd=4, revents=POLLIN}])
ioctl(4, FIONREAD, [131])               = 0
recvfrom(4, "}\210\201\3\0\1\0\0\0\1\0\0\7nginx-1\7cluster\5loc"..., 1024, 0, {sa_family=AF_INET, sin_port=htons(53), sin_addr=inet_addr("172.17.255.10")}, [16]) = 131
close(4)                                = 0
write(2, "ping: unknown host nginx-1\n", 27ping: unknown host nginx-1
) = 27
exit_group(2)                           = ?
```

`__libc_res_nsearch` -> `__libc_res_nquerydomain` -> `__libc_res_nquery` -> `__libc_res_nsend`.

```
int
__libc_res_nsearch(res_state statp,
		   const char *name,	/* domain name */
		   int class, int type,	/* class and type of query */
		   u_char *answer,	/* buffer to put answer */
		   int anslen,		/* size of answer */
		   u_char **answerp,
		   u_char **answerp2,
		   int *nanswerp2,
		   int *resplen2)
{
///...
	/*
	 * We do at least one level of search if
	 *	- there is no dot and RES_DEFNAME is set, or
	 *	- there is at least one dot, there is no trailing dot,
	 *	  and RES_DNSRCH is set.
	 */
	if ((!dots && (statp->options & RES_DEFNAMES) != 0) || /// ex: hostname
	    (dots && !trailing_dot && (statp->options & RES_DNSRCH) != 0)) {
		int done = 0;

		for (domain = (const char * const *)statp->dnsrch; ///try every entry if failed
		     *domain && !done;
		     domain++) {
			searched = 1;

			if (domain[0][0] == '\0' ||
			    (domain[0][0] == '.' && domain[0][1] == '\0'))
				root_on_list++;

			ret = __libc_res_nquerydomain(statp, name, *domain,
						      class, type,
						      answer, anslen, answerp,
						      answerp2, nanswerp2,
						      resplen2);
```

在`/etc/resolv.conf`中配置`options:use-vc`，让getaddrinfo使用TCP访问Server（glibc 2.14及以上.

```
int
__libc_res_nsend(res_state statp, const u_char *buf, int buflen,
		 const u_char *buf2, int buflen2,
		 u_char *ans, int anssiz, u_char **ansp, u_char **ansp2,
		 int *nansp2, int *resplen2)
{
//...
		if (__builtin_expect (v_circuit, 0)) { /// use tcp
			/* Use VC; at most one attempt per server. */
			try = statp->retry;
			n = send_vc(statp, buf, buflen, buf2, buflen2,
				    &ans, &anssiz, &terrno,
				    ns, ansp, ansp2, nansp2, resplen2);
			if (n < 0)
				return (-1);
			if (n == 0 && (buf2 == NULL || *resplen2 == 0))
				goto next_ns;
		} else {
			/* Use datagrams. */
			n = send_dg(statp, buf, buflen, buf2, buflen2,
				    &ans, &anssiz, &terrno,
				    ns, &v_circuit, &gotsomewhere, ansp,
				    ansp2, nansp2, resplen2);
			if (n < 0)
				return (-1);
			if (n == 0 && (buf2 == NULL || *resplen2 == 0))
				goto next_ns;
			if (v_circuit)
			  // XXX Check whether both requests failed or
			  // XXX whether one has been answered successfully
				goto same_ns;
		}
```

参考 [getaddrinfo](http://man7.org/linux/man-pages/man3/getaddrinfo.3.html).
