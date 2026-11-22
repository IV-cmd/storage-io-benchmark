# fio read-path comparison

`make validate SCRATCH=dir` runs six matched read workloads through iobench and
fio: five seconds of measurement after one second of warmup, three alternating
tool/fio pairs per cell, with medians reported. The throughput tolerance remains
**10%**. Latency is reported, not gated: its timing boundaries and percentile
methods differ. Agreement is workload- and host-specific, not universal.

## Reproduce and inspect

Run `make validate SCRATCH=/existing/block-backed/directory`, or dispatch
[Repeated fio validation](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/validate.yml)
on GitHub. Exit **3** means at least one throughput tolerance miss; other
nonzero statuses mean execution, argument, or script errors. The dedicated
workflow fails on a miss. Main CI uses shorter, two-second single pairs and
records misses as warnings, but still fails on script errors.

`results/validate-*/` contains `settings.json` (commit, run ID, duration,
warmup, repeat count, file size, tolerance), `environment.txt` (kernel, fio
version, mount, devices), and `comparison.csv`/`comparison.md`. Each workload
also has the exact `tool-command.txt` and `fio-command.txt`, plus raw
`tool-N.csv` and `fio-N.json` for each repeat. Actions uploads evidence even
when comparisons fail; the repeated bundle is retained for 90 days.

## Parameter equivalence

| iobench | fio | Notes |
|---|---|---|
| `--pattern seq/rand` | `--rw=read/randread --norandommap` | Random blocks are sampled with replacement; seeds and request order differ. |
| `--engine sync` | `--ioengine=psync --direct=0` | Buffered `pread` in both. |
| `--engine odirect` | `--ioengine=psync --direct=1` | Direct `pread` in both. |
| `--engine iouring --iodepth N` | `--ioengine=io_uring --direct=1 --iodepth=N` | Direct asynchronous reads; maximum active request slots, not physical device queue occupancy. |
| `--duration 5s --warmup 1s` | `--runtime=5 --time_based --ramp_time=1` | Same requested windows; different warmup mechanics. |
| Driver pre-reads the file before each buffered run | `--invalidate=0` | No deliberate fio eviction; memory pressure can still evict pages. |
| `latency_ns` | `lat_ns` with `--lat_percentiles=1` | Tool: submit to observed completion; fio: total submission/completion latency. fio p99 is histogram-quantized; the tool interpolates sorted samples. |
| `mib_per_s` | `bw_bytes / 2^20` | Completed logical bytes divided by each tool's elapsed time. |

fio explicitly receives `--iodepth_batch_submit=1`,
`--iodepth_batch_complete_min=1`, `--iodepth_batch_complete_max=1`, and
`--iodepth_low=N`. fio's documented default submission batch is already one;
we must not attribute differences to larger batches without configuring and
measuring them. Matching these knobs does not make the two execution loops or
accounting costs identical.

## Published repeated result

[Run 45015819653](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45015819653),
commit [f4af4295](https://github.com/IV-cmd/storage-io-benchmark/commit/f4af4295bb95bea551aafd0f11540f6aee90666c):
Linux 7.0.0-1020-azure, fio 3.41, ext4 on the runner's virtual `sda1`, populated
1 GiB file, five-second measurements, one-second warmups, three alternating
pairs. Download the `repeated-fio-validation` artifact and inspect
`validate-20261122T110838Z/`. Device names and ROTA do not establish physical
SSD/HDD media.

| workload | tool_mib_s | fio_mib_s | mib_delta_pct | tool_mean_us | fio_mean_us | mean_delta_pct | tool_p99_us | fio_p99_us | verdict |
|---|---|---|---|---|---|---|---|---|---|
| sync-1-seq-4k | 4245.3 | 3442.0 | +23.3 | 0.8 | 1.0 | -13.1 | 1.2 | 1.3 | FAIL |
| sync-1-rand-4k | 3733.4 | 3097.6 | +20.5 | 1.0 | 1.1 | -11.0 | 1.3 | 1.4 | FAIL |
| odirect-1-seq-4k | 30.1 | 30.6 | -1.6 | 129.8 | 127.5 | +1.8 | 239.4 | 242.7 | pass |
| odirect-1-rand-4k | 30.0 | 30.4 | -1.4 | 130.1 | 127.7 | +1.8 | 241.1 | 222.2 | pass |
| odirect-1-rand-64k | 308.4 | 311.1 | -0.9 | 202.5 | 200.5 | +1.0 | 378.4 | 342.0 | pass |
| iouring-32-rand-4k | 76.7 | 76.5 | +0.3 | 1629.2 | 1647.9 | -1.1 | 41691.6 | 41680.9 | pass |

**Four cells pass; both buffered 4 KiB cells fail.** The workflow's failure is
the expected exit 3, not a script crash. These deltas can be recalculated from
the raw completed-byte throughput and repeat medians. The earlier AWS all-pass
table referenced only a directory on its measuring VM; it is not used as
published validation evidence here.

The buffered gap persists across all three pairs. The controlled experiment
below demonstrates instrumentation sensitivity on another runner, but does not
attribute every percentage point of this earlier gap. The original failures
remain recorded; do not widen the tolerance or describe all six cells as passing.

## Cached-read investigation

First, the original three-repeat JSON gives these median mean latencies:

| buffered workload | tool mean, microseconds | fio clat, microseconds | fio lat, microseconds |
|---|---:|---:|---:|
| sequential 4 KiB | 0.835 | 0.935 | 0.961 |
| random 4 KiB | 0.957 | 1.047 | 1.074 |

The synchronous fio engine has no separate slat samples (`N=0`), not a measured
zero submission cost. The lat/clat difference is only about 26–28 ns here.
Reports now expose both means; `fio_mean_us` remains total latency, and
`fio_slat_mean_us` is `n/a` without samples. Switching latency fields cannot
change a throughput verdict.

[Controlled run 45019464171](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45019464171)
then used the same populated, prewarmed 1 GiB file, 4 KiB reads, and exactly
2,000,000 operations per tool/variant, repeated three times with reversed
order on the middle repeat. Both warmup and time-based stopping were disabled.
fio explicitly used `psync`, `direct=0`, `norandommap`,
`random_distribution=random`, and `fadvise_hint=0`. Its `io_size` and
`number_ios` matched the tool's completed-byte and operation budgets.

| workload | tool MiB/s | fio normal MiB/s | fio reduced-statistics MiB/s | normal delta | reduced-statistics delta |
|---|---:|---:|---:|---:|---:|
| seq 4 KiB | 6180.8 | 5521.2 | 6156.4 | +11.9% | +0.4% |
| rand 4 KiB | 5238.6 | 4703.5 | 5232.8 | +11.4% | +0.1% |

The reduced-statistics variant adds `gtod_reduce=1`: fio removes latency and
other timing work. That nearly eliminates the throughput gap **in this
experiment**, demonstrating measurement overhead matters at cached-read speeds.
It is not an acceptance baseline and provides no latency validation. Disabling
only completion percentiles did not remove the gap; changing the clock source
was also tested, not assumed to fix it. Exact commands, settings, CSV/JSON,
and all variants are in `engine-verification/buffered-fixed-operations/`;
`formatted-candidate` preserves the proposed CMake diagnostic driver. No
benchmark C++ changes or diagnostic build targets were committed to main.

A separate file-filtered strace check counted exactly 10,000 `pread64` calls
for each tool. fio `psync` uses `pread`; fio `sync` uses `read` and is not the
same syscall path. Neither our submit-to-observed-completion interval nor fio's
clat is an isolated kernel syscall timer: wrapper and completion work differ.
See the runner [timing boundaries](../src/bench/engine.cc) and fio 3.41's
[engines](https://github.com/axboe/fio/blob/fio-3.41/engines/sync.c) and
[completion accounting](https://github.com/axboe/fio/blob/fio-3.41/io_u.c).

## Profiling scope and limits

`make observe` requests `task-clock,context-switches,cpu-migrations,page-faults`.
These counters cover the entire benchmark process: setup, warmup, measurement,
statistics, and CSV output. Do not divide their totals by the measured window
alone. CI collects as root on its disposable runner without changing kernel
policy, records the collector UID, and restores artifact ownership afterward.
Missing permissions or unsupported events are recorded as unavailable.
[Run 45017058458](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45017058458)
published all four software counters for the three observation workloads.

`iostat` and `blktrace` cover the whole backing device, including unrelated
processes and warmup. All captured output is staged in `/dev/shm` and copied to
`results/` after the monitors stop. Q2G is queue-to-request allocation time;
a small Q2G does not rule out other kernel bottlenecks. D2C is issue-to-completion
through the driver/device path, not isolated physical-media service time.
Tracing also perturbs the workload; these are observations, not an isolated
latency decomposition.

Writes are not cross-validated. These runs do not validate other kernels,
filesystems, hosts, or durability. Repeat on a quiet block-backed host and keep
both successful and failed raw results.
