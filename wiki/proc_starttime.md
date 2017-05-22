`/proc/pid/stat`的第22个字段为进程在内核启动之后，再经过该时间启动，以clock tick为单位。


>              (22) starttime  %llu
                        The time the process started after system boot.  In
                        kernels before Linux 2.6, this value was expressed
                        in jiffies.  Since Linux 2.6, the value is expressed
                        in clock ticks (divide by sysconf(_SC_CLK_TCK)).
>
>                      The format for this field was %lu before Linux 2.6.

即:

` /proc/$pid/stat/starttime` = `process starttime` - `kernel boot`

```
    /proc/$pid/stat/starttime
      /------------------\
______|__________________|________________|___________
kernel boot              process start        now
```

所以，进程进程的墙上时间为`kernel boot` + `/proc/$pid/stat/starttime`。

进程的运行时长= `now` - (`kernel boot` + `/proc/$pid/stat/starttime`)

`/proc/stat`中的`btime`为内核启动的墙上时间。

* procfs

`ps -o lstart -p $pid`可以得到进程的启动时间：

```
# ps -o lstart -p 10420           
                 STARTED
Mon May 22 17:53:42 2017
```


```
static int pr_lstart(char *restrict const outbuf, const proc_t *restrict const pp){
  time_t t;
  t = time_of_boot + pp->start_time / Hertz;
  return snprintf(outbuf, COLWID, "%24.24s", ctime(&t));
}
```
其中`pp->start_time`为`/proc/$pid/stat/starttime`， `Hertz`为USER_HZ，为固定值100。


参考http://man7.org/linux/man-pages/man5/proc.5.html
