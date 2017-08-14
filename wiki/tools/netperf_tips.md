## options

```
Usage: netperf [global options] -- [test options]
```

`netperf`的参数分两部分，一是`global options`，另外是`test options`.

* global options

```
# bin/netperf -help   

Usage: netperf [global options] -- [test options] 

Global options:
    -a send,recv      Set the local send,recv buffer alignment
    -A send,recv      Set the remote send,recv buffer alignment
...
```

* test options

```
# bin/netperf -- -help

Usage: netperf [global options] -- [test options] 

OMNI and Migrated BSD Sockets Test Options:
    -b number         Send number requests at start of _RR tests
    -c                Explicitly declare this a connection test such as
                      TCP_CRR or TCP_CC
    -C                Set TCP_CORK when available
...
```

参考[scan_omni_args](https://github.com/hustcat/netperf/blob/netperf-2.6.0/src/nettest_omni.c#L6894)和[scan_sockets_args](https://github.com/hustcat/netperf/blob/netperf-2.6.0/src/nettest_bsd.c#L13006).

## TCP_RR

```
bin/netperf -H $svrip -p 12865 -l 180 -t TCP_RR -- -r 2048,2048 -P 0,$port &
```
