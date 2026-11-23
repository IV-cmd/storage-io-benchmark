# Workload-specific tuning recommendations

**Starting points for the measured guest only, not hardware-wide defaults.**
All numbers below come from [controlled run 45236536614](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45236536614)
on ext4, virtual `sda1`, Linux 7.0.0-1020-azure, one thread, a populated 256 MiB
file, release build and three repetitions. Rates and percentiles are medians of
individual runs. The [analysis](analysis.md#evidence-and-method) gives preparation,
raw artifact paths, ranges and counter boundaries; read the [caveats](caveats.md)
before applying anything.

| Workload pattern | Candidate engine / flags / kernel setting | Measured reason and limit |
|---|---|---|
| Sequential read | Buffered `--engine sync --pattern seq --block-size 4k`, depth 1; retain this device's default 128 KiB readahead. | A guest-cold single scan reached **721.5 versus 28.4 MiB/s** with readahead disabled; both read 256 MiB from the partition. This supports prefetch for this scan, not arbitrary larger readahead windows. [Evidence](analysis.md#readahead-cold-sequential-scan-versus-sparse-random-reads). |
| Random read | For a reused working set that fits RAM, buffered sync is appropriate. For uncached parallel reads, start with `--engine iouring --pattern rand --block-size 4k --iodepth 4`; retain the original scheduler `none`. | Warm buffered reads reached **3,686.6 MiB/s**, with zero device reads, versus **28.4** for direct sync. Among direct io_uring runs, depth 4 reached **19,595 IOPS** versus **7,595** at depth 1. Disabling readahead saved **2.020 MiB** of prefetch but did not establish a useful speedup; do not change a shared device globally for that alone. [Cache](analysis.md#page-cache-sequential-and-random-4-kib-reads), [depth](analysis.md#queue-depth-random-4-kib-direct-io_uring-reads). |
| Write-heavy | For a small, repeatedly overwritten hot set **only if volatile completion is acceptable**, buffered `--engine sync --op write --pattern seq --block-size 64k` is a candidate. No durable-write engine or dirty-limit setting is recommended from this experiment. | Buffered completion measured **10,235.1 MiB/s** versus direct **391.4**; buffered interior device writes were **0 KiB/s**. That is acceptance into RAM over a 256 MiB wrapping range, not persisted bandwidth. Neither mode includes fsync; random, long-running and mixed writes were not analyzed. [Evidence](analysis.md#write-heavy-completion-throughput-is-not-durability). |
| Latency-sensitive small I/O | For uncached random 4 KiB reads, start direct io_uring at `--iodepth 1`; increase to 4 only if the throughput tradeoff fits the application's tail budget. Leave scheduler `none` unchanged. | Median p99 was **0.232 ms** at depth 1, **0.295 ms** at 4 and **56.127 ms** at 128. Depth 4 already delivered **99.3%** of depth 128's throughput. `none`/`mq-deadline` random-depth-32 p99 ranges overlapped: **39.373–40.893 / 38.571–41.152 ms**. No scheduler tail winner was demonstrated. [Depth](analysis.md#queue-depth-random-4-kib-direct-io_uring-reads), [scheduler](analysis.md#io-scheduler-observe-tails-do-not-assume-a-winner). |

## Applying a setting is a separate experiment

Do not copy the device name or tune a production/shared root disk from this table.
Identify the backing device, record its current setting, change **one factor**,
reopen the benchmark file, repeat with matched cache state and restore the
original value. `blockdev --getra/--setra` uses 512-byte sectors: the measured
default was **256 sectors**, not 256 KiB. Scheduler names must appear in that
device's actual list; BFQ was unavailable here. The run's `restored.txt` verifies
256 sectors and `[none] mq-deadline` afterward. Do not change security policy to
obtain counters; unavailable hardware cache events remain unavailable.

Choose the smallest tested depth that meets throughput **and** tail requirements,
not the largest supported depth. Re-measure with the application's file size,
read/write mix, persistence requirements, concurrency and host. A guest throughput
ceiling does not establish bare-metal saturation or a provider's advertised limit.

These observations are not additional fio acceptance passes. The fixed **10%**
throughput tolerance stays unchanged; the [buffered-read discrepancy](analysis.md#fio-buffered-read-gap-partially-reconciled-not-closed)
remains partially reconciled. Do not disable fio statistics or widen tolerance
to manufacture agreement.
