# Storage I/O Benchmark & Linux I/O Path Analysis (C++)

[![CI](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml)

A standalone C++20/Linux learning project for storage measurement. It runs
single-threaded, buffered `pread`/`pwrite` workloads and reports per-operation
latencies, percentiles, throughput, and IOPS. **Not yet validated against fio:**
there are no trusted performance results or SSD/HDD comparisons published here.
Direct I/O, io_uring, kernel-side analysis, and plots are not implemented.

## Build and run

Install the prerequisites in the [setup guide](docs/setup.md), then:

```sh
make build
./build/iobench --help
make test
```

For workload runs, build without debug/sanitizer instrumentation:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel 2
```

**Use a disposable regular file on an existing writable, block-device-backed
mount. Writes overwrite data.** Read setup can create/extend the file and fills
new bytes before timing; it requires write permission too. Symlinks and
non-regular targets are rejected, but the tool does not enforce filesystem type.
Check the mount as described in the setup guide; avoid tmpfs and Docker OverlayFS.

```sh
./build-release/iobench --file /mnt/test/f --pattern seq --op write --block-size 4k --duration 5s
./build-release/iobench --file /mnt/test/f --pattern rand --op read --block-size 64k --duration 5s
./build-release/iobench --file /mnt/test/f --pattern rand --op read --block-size 4k --file-size 16m --warmup 0 --ops 1000 --output csv > results/read.csv
```

Sizes accept bytes or binary `k`, `m`, `g` suffixes, case-insensitively. Times
accept integer seconds, `s`, or `ms`. Defaults: sequential read, 4 KiB blocks,
1 GiB file range, a discarded 1-second warmup, and a 5-second measured phase.
`--ops N` replaces the measured duration, not the warmup. File size must be a
positive block-size multiple. Larger existing files are not shrunk; only the
configured prefix is used. Existing contents are preserved during read setup;
existing sparse/unwritten extents are not detected or populated.

## What the numbers mean

- Sequential offsets advance by one block and wrap at the configured range end.
- Random offsets are uniform over aligned blocks, with replacement; seed is 42.
- Warmup and measured phases restart the offset generator independently.
- `steady_clock` times each complete logical block transfer, including retries.
- Setup, warmup, statistics calculation, and output are excluded from run timing.
- Elapsed run time includes offset generation and in-memory sample recording.
- MiB/s counts completed logical bytes divided by elapsed time and 2^20.
- IOPS counts completed logical block operations divided by elapsed time.

Percentiles use linear interpolation at sorted position `(n - 1) * p / 100`.
The pure stats API returns zero-valued latency statistics for empty input and
rejects nonpositive or nonfinite elapsed time. Engine runs with no
samples fail rather than presenting empty measurements.

**Buffered completion is not durability or device throughput.** There is no
`fsync`, cache eviction, readahead control, or scheduler control. Newly populated
read data and warmup affect the page cache; dirty-page throttling affects writes.
Preallocation happens before timing, but unwritten-extent conversion and other
filesystem work can still affect writes. Writes use a fixed, compressible byte
pattern; compression/deduplication can distort physical-I/O comparisons.
Raw samples use memory proportional to
the operation count; large/long runs can exhaust memory. Avoid concurrent changes
to the target. The time limit is checked between operations, not during a syscall.

## CSV contract

`--output csv` writes only CSV to stdout; errors go to stderr. One file represents
one run: one header, measured `sample` rows, then one `summary` row. No warmup
samples are included. The schema version is `1`:

```text
schema_version,record_type,op,block_size,file_size,pattern,warmup_ns,seq,offset,latency_ns,operations,bytes,elapsed_ns,mib_per_s,iops,min_us,mean_us,p50_us,p95_us,p99_us,max_us
```

Sizes and offsets are bytes. Sample sequence numbers start at zero; sample times
and elapsed/warmup times are nanoseconds. Summary latencies are microseconds.
Sample-only fields are empty on the summary row; summary-only fields are empty
on sample rows. Consumers should check `schema_version` and select rows by
`record_type`, not treat the summary as an extra operation.

## Verification and scope

Four focused tests cover percentile edges, summaries/rates, aligned offsets, and
a tiny write/read run with byte-for-byte verification of the written pattern.
There is no per-operation runtime checksum verification. CI builds, checks Google
formatting, runs curated clang-tidy checks, and runs tests under ASan/UBSan. It
also executes release-mode CLI examples and parses their CSV as a smoke check;
its output artifacts are not validated storage performance results.

No threads, queue-depth setting, direct I/O, io_uring, fio comparison, or coverage
gate is included. MIT licensed; see [LICENSE](LICENSE).
