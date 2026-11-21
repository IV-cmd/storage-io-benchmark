# fio cross-validation

`make validate SCRATCH=dir` runs `scripts/validate_fio.py`: identical read
workloads through iobench and fio 3.28, three alternated tool/fio pairs per
cell, median reported. It fails (exit 1) when any cell's throughput delta
exceeds the tolerance below. Full raw output is under `results/validate-*`
(one `tool-N.csv` and `fio-N.json` per repeat).

## Parameter equivalence

| iobench | fio | Notes |
|---|---|---|
| `--pattern seq/rand` | `--rw=read/randread` | `--norandommap` makes fio sample blocks uniformly with replacement, matching the tool's offset generator. Seeds differ, so random orderings differ. |
| `--engine sync` | `--ioengine=psync --direct=0` | Both buffered `pread`. |
| `--engine odirect` | `--ioengine=psync --direct=1` | Same `O_DIRECT` pread path. |
| `--engine iouring --iodepth N` | `--ioengine=io_uring --direct=1 --iodepth=N` | fio submits multiple SQEs per syscall (batched accounting); this tool waits one CQE then submits one SQE. |
| `--duration 5s --warmup 1s` | `--runtime=5 --time_based --ramp_time=1` | Same measured window; different warmup mechanics. |
| -- | `--invalidate=0` | Stops fio evicting the file; for buffered cells the driver pre-warms the file for both tools, so both read a fully cached file. |
| `latency_ns` | `lat_ns` (`--lat_percentiles=1`) | Tool latency is submit-to-observed-completion; fio `lat` is the total of queue+completion latency. fio percentiles come from a histogram, so p99 is bucket-quantized. |
| `mib_per_s` | `bw_bytes` / 2^20 | Both count completed logical bytes over elapsed time. |

Tolerance is **10% on throughput**, fixed before the first comparison run.
Latency columns are reported but not gated: mean latency mostly mirrors
throughput, and tail buckets differ between the tools. A larger delta is a
bug to investigate, not to explain away.

## Observed deltas

AWS 8-vCPU VM, Linux 6.8.0-1060-aws, ext4 on `vda1` (virtio, scheduler
`[none]`), 1 GiB populated file, 5s measured + 1s warmup, 3 alternated
repeats. Raw data: `results/validate-20261121T192922Z/` on the measuring VM.

| workload | iobench MiB/s | fio MiB/s | delta | mean us (tool/fio) | verdict |
|---|---:|---:|---:|---|---|
| sync seq 4k | 2118.2 | 2001.1 | +5.9% | 1.7 / 1.8 | pass |
| sync rand 4k | 1870.4 | 1741.8 | +7.4% | 2.0 / 2.0 | pass |
| odirect seq 4k | 241.1 | 247.5 | -2.6% | 16.1 / 15.5 | pass |
| odirect rand 4k | 209.4 | 225.6 | -7.2% | 18.5 / 17.0 | pass |
| odirect rand 64k | 1619.4 | 1625.1 | -0.4% | 38.5 / 38.0 | pass |
| iouring-32 rand 4k | 671.6 | 674.6 | -0.4% | 186.0 / 185.0 | pass |

All six cells pass. Buffered reads trend a few percent high (fio's per-op
accounting overhead at microsecond latencies); O_DIRECT reads trend slightly
low for the same reason reversed — fio's submission path is leaner when I/O
latency dominates. The io_uring cell is within noise.

## Scope

Writes are excluded: the tool's per-op latencies only cover what it measures,
and fio's buffered-write semantics (dirty-page throttling, background
writeback) need a dedicated comparison. This is read-path validation on one
virtual disk; it does not validate the numbers on other kernels, filesystems,
or hardware. Repeat on a quiet host — shared VMs add noise comparable to the
tolerance at short durations, which is why CI records the comparison without
gating on the delta.
