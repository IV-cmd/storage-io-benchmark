# Storage I/O Benchmark & Linux I/O Path Analysis (C++)

[![CI](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml)

A standalone C++20/Linux learning project comparing buffered `pread`/`pwrite`,
`O_DIRECT`, and direct `io_uring` workloads. It reports per-operation latencies,
percentiles, throughput, and IOPS for sequential/random reads and writes.
**Not yet validated against fio:** exploratory observations are not trusted
storage-performance results or SSD/HDD comparisons. Kernel-side profiling and
plots are not implemented.

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

## Build and run

Install the prerequisites in the [setup guide](docs/setup.md), then:

```sh
make build
./build/iobench --help
make test
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
performance differences are not CI pass thresholds.

See the [cache-bypass observation](docs/cache-bypass.md) for actual iostat and
file-residency evidence, and [learning notes](LEARNING_LOG.md) for failures and
limits. No fio comparison, perf/blktrace analysis, plotting, or coverage gate is
included. MIT licensed; see [LICENSE](LICENSE).
