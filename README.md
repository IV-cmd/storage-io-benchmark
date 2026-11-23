# Storage I/O Benchmark & Linux I/O Path Analysis (C++)

[![CI](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml)

A standalone C++20/Linux learning project comparing buffered `pread`/`pwrite`,
`O_DIRECT`, and direct `io_uring` workloads. It reports per-operation latencies,
percentiles, throughput, and IOPS for sequential/random reads and writes.
Read workloads are compared with fio at a fixed 10% throughput tolerance.
The published repeated run passes four of six cells; buffered 4 KiB reads remain
outside tolerance (see [validation](docs/validation.md)). Writes are not yet
cross-validated.
`iostat`, `blktrace`/`btt`, and `perf stat` capture is implemented; plots are not.

## Engines

| `--engine` | Data path | `--iodepth` |
|---|---|---|
| `sync` (default) | Buffered `pread`/`pwrite` | 1 |
| `odirect` | `pread`/`pwrite` with `O_DIRECT` | 1 |
| `iouring` | liburing reads/writes with `O_DIRECT` | Positive request-slot limit |

There is one userspace thread; the kernel may use workers. Liburing handles ring
mapping and memory ordering so the project can focus on workload/accounting code.
The synchronous engines reject a depth other than one. Direct engines query
filesystem alignment with `statx` and device logical block size through sysfs,
allocate aligned buffers, and reject misalignment rather than fall back.
See [direct-I/O caveats](docs/caveats.md), including actual tmpfs behavior.

## Measured findings

[I/O path analysis](docs/analysis.md) · [Workload tuning](docs/tuning.md) ·
[Encountered caveats and limits](docs/caveats.md)

Snapshot from [controlled run 45236536614](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45236536614):
release build, ext4 on virtual `sda1`, populated 256 MiB file, three repetitions.
These are guest-path observations, **not physical-media ratings or new fio
acceptance passes**. The analysis records raw artifacts, cache state and ranges.

| Workload pattern | Measured finding | Conditional starting point |
|---|---|---|
| Sequential read, cold-start 4 KiB scan | Default readahead: **721.5 MiB/s**, disabled: **28.4**; both fetched 256 MiB. | Retain the tested 128 KiB default for this scan. |
| Random read, cache-fitting 4 KiB working set | Warm buffered: **3,686.6 MiB/s**, with zero partition reads. | Use buffered reads when reuse and RAM capacity fit; this is not disk bandwidth. |
| Write-heavy, sequential 64 KiB hot set | Buffered: **10,235.1 MiB/s** accepted, with **zero interior device writes**. | Volatile buffering only; no durable-write tuning claim. |
| Latency-sensitive random 4 KiB direct I/O | io_uring depth 4/128 p99: **0.295/56.127 ms**; depth 4 delivered **99.3%** of depth 128 throughput. | Start near the depth-4 knee for throughput, depth 1 for lower tails. |

The tested `none`/`mq-deadline` p99 ranges overlapped; no scheduler winner was
established. BFQ was not exposed, and the hardware cache-event-pair probe failed;
neither has invented measurements. The buffered fio discrepancy remains
[partially reconciled](docs/analysis.md#fio-buffered-read-gap-partially-reconciled-not-closed),
with the fixed tolerance unchanged. No bare-metal or power-loss behavior was tested.

## Build and run

Install the prerequisites in the [setup guide](docs/setup.md), then:

```sh
make build
./build/iobench --help
make test
make release                    # optimized binary for measurements
make validate SCRATCH=/mnt/test # side-by-side iobench vs fio deltas
make observe SCRATCH=/mnt/test  # iostat + blktrace/btt + perf stat bundle
```

Use a release build, without sanitizers, for workload observations:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel 2
```

**Use a disposable regular file on an existing writable, block-device-backed
mount. Writes overwrite data.** Read setup creates/extends files and fills new
bytes before timing; it needs write permission too. Symlinks and non-regular
files are rejected. Direct engines reject tmpfs and OverlayFS; buffered mode
still needs a manual mount check. Avoid concurrent changes to the target.

```sh
./build-release/iobench --engine sync --file /mnt/test/f --pattern seq --op write --block-size 4k --duration 5s
./build-release/iobench --engine odirect --file /mnt/test/f --pattern rand --op read --block-size 64k --duration 5s
./build-release/iobench --engine iouring --iodepth 32 --file /mnt/test/f --pattern rand --op read --block-size 4k --duration 5s
./build-release/iobench --engine iouring --iodepth 32 --file /mnt/test/f --file-size 16m --ops 1000 --warmup 0 --output csv > results/read.csv
```

Sizes accept bytes or binary `k`, `m`, `g` suffixes, case-insensitively. Times
accept integer seconds, `s`, or `ms`. Defaults: sequential read, 4 KiB blocks,
1 GiB file range, discarded 1-second warmup, and 5-second measured phase.
`--ops N` replaces measured duration, not warmup. File size must be a positive
block-size multiple. Larger existing files are not shrunk; only the configured
prefix is used. Existing sparse/unwritten extents are not detected or filled.

## What the numbers mean

- Sequential offsets advance by one block and wrap at the configured range end.
- Random offsets are uniform over aligned blocks, with replacement; seed is 42.
- Warmup drains before measurement; each phase restarts the offset generator.
- `steady_clock` measures request preparation to observed completion.
- Async latency includes queueing and userspace delay before reaping a CQE.
- Setup, warmup, statistics, and output are excluded from measured elapsed time.
- Elapsed time includes offset generation, sample recording, and final draining.
- MiB/s counts completed logical bytes divided by elapsed seconds and 2^20.
- IOPS counts completed logical block operations divided by elapsed seconds.

After duration expires, no new requests start; outstanding requests finish before
timing ends. Percentiles interpolate at sorted position `(n - 1) * p / 100`.
Empty pure-statistics input returns zero latencies; empty engine runs fail.

**Neither buffered completion nor `O_DIRECT` means durable storage.** The engines
do not fsync, evict caches, or control readahead/schedulers. Read preparation is
outside timing but affects cache state; dirty-page pressure affects buffered
writes. Preallocation does not eliminate all filesystem work. Writes use a fixed,
compressible byte pattern. Raw samples consume memory proportional to operation
count, so long runs can exhaust RAM. A blocked syscall can exceed the time limit.

## CSV contract

`--output csv` writes only CSV to stdout; errors go to stderr. One run has one
header, measured `sample` rows, and one `summary` row. No warmup samples appear.
Schema **2** adds engine/depth metadata and summary accounting. Existing field
names/units remain unchanged; consumers must check the version, not assume the
column positions from schema 1.

```text
schema_version,record_type,engine,iodepth,op,block_size,file_size,pattern,warmup_ns,seq,offset,latency_ns,operations,bytes,elapsed_ns,mib_per_s,iops,min_us,mean_us,p50_us,p95_us,p99_us,max_us,submitted,completed,max_inflight
```

Sizes/offsets are bytes; sample, elapsed, and warmup times are nanoseconds;
summary latencies are microseconds. `seq` starts at zero and denotes observed
completion order, not submission order. Sample-only fields are empty on the
summary; summary-only fields are empty on samples. `submitted` and `completed`
exclude warmup and must equal measured operations. `max_inflight` is the peak
occupied request-slot count, **not measured hardware queue occupancy**.

## Verification and scope

Six focused tests cover math, offsets, a tiny write/read integrity check,
alignment rejection before I/O, and exact io_uring accounting with partial/full
queues. There is no per-operation runtime checksum. CI checks build, formatting,
clang-tidy, and tests under ASan/UBSan without suppressions. Release checks run
all engines over both patterns/operations and three block sizes, parse CSV, and
check written bytes. Separate scripts collect exploratory cache/depth evidence;
main CI records fio tolerance misses as warnings, while the dedicated repeated
validation workflow fails on them.

See the [analysis](docs/analysis.md) for measured cache, readahead, scheduler,
queue-depth and write behavior, [tuning](docs/tuning.md) for conditional choices,
and [caveats](docs/caveats.md) for encountered environment traps and untested limits.
The [earlier cache observation](docs/cache-bypass.md),
[fio validation](docs/validation.md), and [learning notes](LEARNING_LOG.md)
retain the original evidence and investigation. No plots or coverage gate are included.
MIT licensed; see [LICENSE](LICENSE).
