# C webserver

This code is an artisanal, handwritten webserver written in C with an incremental parser allowing ridiculously small TCP buffers of like 8 bytes. NO CLANKERS ALLOWED!

Concurrency is built with kqueue on macos.

```sh
❮ wrk -t6 -c400 -d10s http://127.0.0.1:80
Running 10s test @ http://127.0.0.1:80
  6 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.85ms  637.56us  61.52ms   99.24%
    Req/Sec    34.22k     3.07k   40.63k    71.62%
  2063734 requests in 10.10s, 222.40MB read
Requests/sec: 204270.66
Transfer/sec:     22.01MB
```

## Building

```
make all
make debug
```
