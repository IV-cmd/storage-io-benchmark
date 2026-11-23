# I/O path analysis

These are observations of a guest storage path, not a physical-disk ranking or
proof of universal fio agreement. [Tuning](tuning.md) turns the results into
conditional choices; [caveats](caveats.md) states what was actually encountered.

## Evidence and method

**A — [controlled path experiment](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45236536614).**
Linux 7.0.0-1020-azure, four guest CPUs, ext4 on `/dev/sda1`, model `Virtual Disk`,
logical/physical blocks 512/4096 bytes. ROTA=1 does **not** identify HDD media.
Default scheduler `none`; available choices `none mq-deadline`. Default readahead
256 sectors = 128 KiB. Mount options included `nobarrier,data=writeback`:
this is not a durability test environment.
Release binary from [8a19efb](https://github.com/IV-cmd/storage-io-benchmark/commit/8a19efb25fa794de2a2af1ac8991a119fe155894);
only a temporary CMake collection target was supplied through the existing
manual workflow. No benchmark source changed. Its `formatted-candidate` artifact
preserves that target; `engine-verification/io-path-analysis/` contains
`collect.py`, `environment.json`, `metrics.csv`, and each trial's `command.txt`,
`tool.txt`, `metrics.json`, `iostat.txt`, and timestamped `iostat-samples.json`.
Cache trials also have `perf.csv`. The [evidence artifact](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45236536614/artifacts/14850462580)
contains 63 controlled trials. Tables below use A unless another run is named.

The collector populated and fsynced one disposable **256 MiB regular file**.
Before every trial it fsynced and requested file-scoped eviction with
`POSIX_FADV_DONTNEED`, verifying zero resident bytes with `fincore`. Warm trials
then read the entire file and verified full residency. "Cold" means guest file
pages were absent, not that provider/controller caches were cold. No global
`drop_caches` was used. All trials disabled benchmark warmup.

There were three repetitions, reversing each case list in the middle repetition.
Numbers are medians of per-run summaries, **not pooled latency percentiles**.
Ranges below are the three observed values' min–max, not confidence intervals.
Cache, scheduler and write trials measured three seconds; queue-depth trials five.
Readahead trials used fixed operation counts. Submitted, completed and measured
counts matched in every trial; byte/rate arithmetic was checked.

`iostat` ran at one-second intervals. `aqu-sz` below is the median of per-run
means of interior samples: collection timestamps after process start +1.2 s
and before start + reported elapsed −0.2 s. These exclude edges approximately;
the benchmark has no phase markers. Partition sector deltas span the whole
process, while iostat covers its parent disk. Both include unrelated activity.
Counters use 512-byte accounting sectors, not the device's alignment size.
Captures stayed in tmpfs until monitors stopped. Original readahead and scheduler
were read back after restoration in `restored.txt`.

To repeat, download A's artifacts, inspect `collect.py`, build release, and run
the collector with binary, scratch directory, commit and run-label arguments.
It changes **device-wide** settings: use only a disposable, otherwise idle VM.
Exact commands and ordering are preserved, including preparation and restoration;
do not run the collector against production storage. Actions artifacts expire:
download the bundle if retaining this experiment beyond repository retention.

## Page cache: sequential and random 4 KiB reads

Source: A, `cache-{seq,rand}-{sync,odirect}-{1,2,3}/`.

| Pattern / engine | MiB/s | p99, µs | Resident after, MiB | Partition reads, MiB | Interior `aqu-sz` |
|---|---:|---:|---:|---:|---:|
| Sequential / buffered sync, warm | 4,197.1 | 1.152 | 256 | 0 | 0.00 |
| Sequential / direct sync, cold | 26.5 | 249.487 | 0 | 79.531 | 0.92 |
| Random / buffered sync, warm | 3,686.6 | 1.283 | 256 | 0 | 0.00 |
| Random / direct sync, cold | 28.4 | 238.953 | 0 | 85.324 | 0.94 |

The two paths did not perform equivalent device work: buffered trials deliberately
reused resident data, whereas direct trials started with no file pages resident.
That is the cache effect being tested, not evidence that RAM throughput is a
device's read speed. Warm trials started and ended with all 65,536 file pages
resident; direct trials started and ended with zero. All warm-trial iostat samples
reported zero reads and writes. For example, `cache-seq-sync-1/iostat.txt` at
18:18:19 UTC shows `r/s=0`, `rkB/s=0`, `aqu-sz=0`, `%util=0`.
This combination establishes guest file-page-cache bypass for the direct trials,
not the absence of every storage cache.

The code's paths are:

```text
buffered hit: pread -> VFS/filesystem -> page-cache lookup/copy -> userspace
buffered miss:                          -> block layer -> driver/provider
                                      <- populate page cache <- completion
direct read: pread -> VFS/filesystem direct path -> block layer -> driver/provider
io_uring: SQE -> filesystem direct path -> same block/device path -> CQE -> reap
```

`O_DIRECT` skips the guest file-data page cache, **not VFS, filesystem mapping,
kernel work, or provider caches**. This io_uring engine also uses direct I/O;
its benefit can come from overlapping requests, not skipping more path layers.
Warm buffered time still includes syscall/VFS work, memory copying, offset
generation, clocks and sample recording. We did not separately time those layers.
See [the runner](../src/bench/engine.cc) and [synchronous path](../src/bench/sync.cc).

### What perf and the block trace establish

| Cache trial | Whole-process page faults | Whole-process context switches |
|---|---:|---:|
| Sequential buffered, warm | 35,423 | 19 |
| Sequential direct, cold | 367 | 20,360 |
| Random buffered, warm | 33,126 | 20 |
| Random direct, cold | 374 | 21,843 |

These perf totals cover the **whole process**, including statistics and output;
the faster trials also complete many more operations. A page fault is a process
virtual-memory event, not a count of `pread` page-cache misses. Allocating the
latency/sample vectors also contributes. Hardware `cache-misses`, when supported,
means CPU-cache events, **not Linux file-page-cache misses**.
The hardware probe exited 1: `No supported events found. The cache-references
event is not supported.` Therefore no hardware cache-miss count is claimed.
The software counters succeeded as root; the many direct-run context switches
are consistent with synchronous waiting, not a separately measured kernel-cost
breakdown. File residency and device reads, not perf faults, support the cache claim.

For an independently retained block-layer capture, **B —
[run 45020009979](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45020009979)**,
`engine-verification/observe-20261122T120311Z/*/btt.txt` reports:

| Captured read workload | Mean Q2G, µs | Mean D2C, µs | D2C observations |
|---|---:|---:|---:|
| Buffered sequential 64 KiB | 0.475 | 524.355 | 8,192 |
| Direct random 4 KiB, depth 1 | 0.417 | 126.994 | 22,241 |
| io_uring random 4 KiB, depth 32 | 0.561 | 1,510.134 | 62,280 |

Those are different workloads on another runner, not a paired decomposition of A.
The trace covers setup/warmup and unrelated processes; it even lists `.NET TP
Worker` and `blktrace`. Q2G measures queue-to-request allocation only. D2C includes
driver/device completion, not isolated physical-media service time. Its dominance
does not prove the rest of the kernel is free. B's supposedly "buffered" trace
contains device reads: buffered does not mean the entire measured file was warm.

## Readahead: cold sequential scan versus sparse random reads

Source: A, `readahead-{seq,rand}-*-{1,2,3}/`. Each sequential trial reads the whole
file once: 65,536 × 4 KiB = 256 MiB. Each random trial requests 16,384 × 4 KiB =
64 MiB, with replacement over the same range and seed. Repeated random offsets
can hit pages already fetched within that trial. The default and zero settings
were applied with `blockdev --setra`, whose units are **512-byte sectors**.

| Pattern | Readahead, KiB | MiB/s | p50, µs | Partition reads, MiB | Device / requested bytes |
|---|---:|---:|---:|---:|---:|
| Sequential | 128 (default) | 721.5 | 0.821 | 256.000 | 1.000 |
| Sequential | 0 | 28.4 | 130.223 | 256.000 | 1.000 |
| Random | 128 (default) | 32.65 | 127.529 | 58.637 | 0.916 |
| Random | 0 | 32.99 | 125.615 | 56.617 | 0.885 |

The sequential median rate was **25.4×** higher at default readahead despite
identical total device bytes. Default-run rates ranged 414.6–744.1 MiB/s versus
28.0–28.4 without readahead; the benefit survives that variation. The default
scan's sub-microsecond p50, after a verified cold start and with no wraparound,
is consistent with prefetched pages serving later requests cheaply. This is
amortizing/overlapping storage reads, not avoiding the initial 256 MiB transfer.

For identical random offsets, default readahead fetched **2.020 MiB / 3.6% more**
than the disabled setting. File residency afterward also measured 58.637 versus
56.617 MiB in every repetition, strengthening attribution to extra file prefetch
rather than unrelated device traffic. That is modest unused prefetch in this
sequence, not "one full window wasted per random request." Both ratios remain
below 1 because requested bytes include duplicate offsets. Random throughput
ranges overlapped: 32.10–34.23 MiB/s default, 32.61–34.40 disabled. Do not claim
a meaningful speedup from disabling readahead based on this result.

This tests the kernel's existing adaptive behavior, not a forced sequential
`fadvise` hint on random I/O. Do not infer that a large configured readahead
window is fetched after every random read. Requested bytes and device bytes are
different quantities; filesystem/background activity can also affect their ratio.
Short fast scans have no full interior iostat sample: use partition counter
deltas there, not an invented zero `aqu-sz`.

## I/O scheduler: observe tails, do not assume a winner

Source: A, `scheduler-{seq,rand}-*-{1,2,3}/`. The sequential case uses synchronous
direct 64 KiB reads at depth 1; random uses direct io_uring 4 KiB at depth 32.
Only the scheduler changes **within each pattern**. Readahead is restored to the
original setting. Percentiles are submit-to-observed-completion, not btt D2C.

| Pattern / scheduler | MiB/s | p50, µs | p95, µs | p99, µs | p99 min–max, µs |
|---|---:|---:|---:|---:|---:|
| Sequential / none | 385.0 | 153.495 | 198.667 | 266.411 | 265.305–284.636 |
| Sequential / mq-deadline | 381.0 | 154.900 | 201.713 | 264.615 | 258.444–278.100 |
| Random / none | 77.11 | 272.540 | 656.368 | 39,382.126 | 39,373.122–40,892.830 |
| Random / mq-deadline | 77.09 | 292.496 | 659.129 | 38,915.545 | 38,571.438–41,152.006 |

Both patterns' p99 ranges overlap, as do throughput ranges. There is no clear
tail-latency winner in these three repetitions, so retain the original `none`
rather than recommend a switch. BFQ was not offered and was not measured.
At random depth 32, changing the guest scheduler did not remove the roughly
39 ms p99 tail; this does not identify whether the limit is in the guest,
driver or provider. It argues against claiming that a scheduler switch fixed it.

The available-scheduler list and selected bracketed value were checked before
and after each change. No modules were loaded to manufacture a third choice.
These are single-job read tests, not competing readers/writers or fairness tests.
A scheduler can only order requests that reach it; a warm page-cache hit does not
need that block queue. See the kernel's [switching instructions](https://docs.kernel.org/block/switching-sched.html).

## Queue depth: random 4 KiB direct io_uring reads

Source: A, `depth-{1,4,8,16,32,64,128}-{1,2,3}/`. Identical engine, file, pattern,
block size and default scheduler; only the request-slot limit changes.

| Depth | MiB/s | IOPS | Mean, µs | p99, ms | Interior `aqu-sz` |
|---|---:|---:|---:|---:|---:|
| 1 | 29.67 | 7,595 | 131.539 | 0.232 | 0.94 |
| 4 | 76.54 | 19,595 | 204.022 | 0.295 | 3.77 |
| 8 | 77.01 | 19,716 | 405.632 | 0.548 | 7.77 |
| 16 | 76.81 | 19,664 | 813.526 | 36.395 | 15.72 |
| 32 | 77.01 | 19,714 | 1,622.998 | 40.037 | 30.35 |
| 64 | 76.94 | 19,696 | 3,249.019 | 53.137 | 58.43 |
| 128 | 77.08 | 19,732 | 6,486.563 | 56.127 | 115.02 |

The useful throughput knee was **depth 4**, delivering 99.3% of depth 128's
median throughput. Moving from 4 to 128 gained only 0.7%, while p99 rose from
0.295 to 56.127 ms and average outstanding block requests rose from 3.77 to
115.02. Depth 4 and 128 throughput ranges overlapped (76.45–76.95 versus
76.83–77.22 MiB/s). The tail jump at depth 16 is real in these repeats:
p99 ranged 35.651–36.895 ms even though its median p50 was only 0.160 ms.
Typical latency alone would hide this behavior. Consider depth 4 for throughput,
or depth 1 when a lower tail matters more: its p99 range was 0.222–0.280 ms,
versus 0.291–0.387 at depth 4. These are observations, not latency guarantees.

`aqu-sz` is an average number of outstanding guest block requests; configured
depth and `max_inflight` describe application slots. They are not interchangeable,
nor a physical-device queue measurement. A throughput plateau with rising latency
locates a limit of this **tested guest path**; provider limits, CPU and virtual
device behavior are not separated. It does not identify the physical medium.

## Write-heavy: completion throughput is not durability

Source: A, `write-seq-{sync,odirect}-{1,2,3}/`. Three-second sequential 64 KiB writes,
depth 1, wrap over the same 256 MiB range. Both engines omit fsync from timing.
Preparation fsyncs before the next trial, not inside the reported result.

| Engine | MiB/s accepted | p99, µs | Resident after, MiB | Interior device writes, KiB/s |
|---|---:|---:|---:|---:|
| Buffered sync | 10,235.1 | 11.571 | 256 | 0 |
| Direct sync | 391.4 | 219.075 | 0 | 406,016 |

The buffered trial's interior iostat samples all showed zero device writes,
despite high logical throughput. The direct path issued device writes during
the same kind of window. The write-rate column is derived from the same interior
samples as `aqu-sz`, not an exact application-only byte total. Neither engine
uses fsync or O_SYNC, and provider cache/power-loss behavior was not tested.
Repeated buffered writes can dirty the same pages repeatedly. These numbers
measure bytes accepted by each path, not unique bytes persisted to media.
No dirty-limit, durability, long-running writeback or mixed-workload tuning
recommendation follows. Writes still lack fio cross-validation.

## fio buffered-read gap: partially reconciled, not closed

**C — [three-pair comparison](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45015819653)**
keeps the 10% throughput tolerance: buffered sequential/random 4 KiB reads miss
by **+23.3%/+20.5%**, while four direct-I/O cells pass. fio's lat−clat mean
difference was only 26–28 ns; synchronous psync had no separate slat samples.
Choosing clat cannot change a throughput gate.

**D — [fixed-count diagnostic](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45019464171)**
completed 2,000,000 reads per tool/variant; separate syscall checks counted
10,000 `pread64` calls each. The tool's sequential/random throughput advantages
over normal fio were **11.9%/11.4%**. With diagnostic `gtod_reduce=1`, they became
**0.4%/0.1%**. Reducing fio's timing/statistics work therefore materially changes
this cached-read comparison. It does not identify each internal cost or explain
every point of C's larger gap on another runner.

Keep normal fio measurement enabled for acceptance; reduced statistics cannot
validate latency. Both tools' timing boundaries contain wrapper/observation work,
not isolated syscall service time. psync matches `pread`; fio sync would use
`read`. The discrepancy remains an open cross-tool comparability question, with
evidence of instrumentation sensitivity—not an established latency-field fix.
Full commands, latency fields and raw references are in [validation](validation.md#cached-read-investigation).
