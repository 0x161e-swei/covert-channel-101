# AWS LLC Prime+Probe and Flush+Reload Implementations
This branch currently works on
- an aws i3-metal machines
- an aws m4.xlarge dedicated instance
- a local Haswell (4c/8t) desktop




# Chat on an LLC covert channel

This repository contains a sender and a receiver that can be used for a cross-process exchange of messages
over a hardware covert channel on the last level cache.

The project was done as part of the course [CS598CLF - Secure Processor Design][CS598CLF] held in the fall 2017 semester
at the University of Illinois at Urbana-Champaign.

The project guidelines are available at: http://cwfletcher.net/Content/598/lab598clf_v0-2.pdf

## How to use

The client consists of a sender and a receiver, where the former allows to send messages to the latter. 
The first step to use them is to compile both the sender and the receiver with the commands:

```sh
make -f Makefile_sender
make -f Makefile_receiver
```

### Basic usage

![Basic usage](./usage/basic.gif?raw=true)

### Benchmarks

The sender has a flag `-b` that allows to measure the bandwidth in Bytes/second.
Here are some examples of bandwidths that can be achieved with our client.

#### Normal bandwidth

This is the default bandwidth, which achieves a 10x speedup compared to the TA solution while maintaining a very
good accuracy.

![Benchmark usage](./usage/benchmark.gif?raw=true)

#### High bandwidth

This is the bandwidth that our client can achieve when the time interval of sending one bit is lowered to 30 
cycles/bit. It is still quite accurate, even though sometimes the receiver might flip some bits in its received 
message (depending on the system noise). 

![Fast benchmark](./usage/benchmark_fast.gif?raw=true)

#### Very high bandwidth

This is the bandwidth that our client can achieve when the time interval of sending one bit is lowered to 20 
cycles/bit. Now it starts to be less accurate, and the receiver will often flip some bits in its received message 
(depending on the system noise). However, after a few tries, it is possible to get perfect accuracy. The bandwidth is 
100x higher than one of the TA solution.

![Very fast benchmark](./usage/benchmark_fast_fast.gif?raw=true)

Potentially one could achieve even faster bandwidths by playing with the `-i` parameter in the sender and the receiver
and the `-w` parameter in the receiver, but the accuracy may become worse. 

## Description of the covert channel

We started our implementation using the ideas explained in the paper [C5: Cross-Cores Cache Covert Channel][c5] by
C. Maurice et al. presented at DIMVA 2015. The covert channel described in that paper works in three steps:

1. The receiver probes one set of lines repeatedly: the access is fast because the data is in its L1 (and the LLC by
inclusive feature) cache.
2. The sender fills the LLC, thus evicting the set of the receiver from the LLC and its private L1 cache.
3. The receiver probes the same set: the access is slow because the data must be retrieved from RAM.

However, this covert channel had two limitations:

1. It achieved a maximum speed of only about ~161 Bytes/second, since it required the sender to evict the entire LLC.
2. It was easily susceptible to transmission errors.

We fixed the aforementioned limitations by:

1. Making the sender evict (in our case flush) only a small fraction of the LLC, precisely a set of addresses whose
cache set index is `0`. The receiver will also only probe addresses with the same property. This allowed us to 
considerably speed up the covert channel.
2. Adding synchronization between sender and receiver, so to prevent transmission errors.

## Extensions implemented

Out of the several extensions proposed in the [lab document][deaddrop], we implemented:
- SpeedRun up to 100x and more: as explained in the benchmarks section above.
- AnyCore: since our client uses the LLC for its covert channel, it works also when the sender and the receiver are
running on different cores.
- AnyCore++: our client does not use RDRAND/RDSEED.

## Challenges we ran into

- We originally planned to use an LLC covert channel which would work by taking addresses in only certain cache slices
that the sender and the receiver agree upon. However, our implementation of that approach never worked and, after
several attempts, we decided to give up on understanding the obscure function that maps addresses to cache slices on
Intel processors.

- The other challenges we ran into were technical implementation details not mentioned in the any of the papers 
we read that we had to figure out by trial and error. We documented them in comments in the source code.

[CS598CLF]: http://cwfletcher.net/598clf.html
[deaddrop]: http://cwfletcher.net/Content/598/lab598clf_v0-2.pdf
[c5]: http://www.s3.eurecom.fr/docs/dimva15_clementine.pdf
