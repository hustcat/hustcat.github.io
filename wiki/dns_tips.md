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
