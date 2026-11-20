# First cache-bypass and queue-depth observation

This is exploratory evidence, **not fio-validated performance**. Numbers below
come from [Actions run 21](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/44724286571)
at [0c88553](https://github.com/IV-cmd/storage-io-benchmark/commit/0c885534213eea7e606a8297011109cdb4498f48).
Its `engine-verification` artifact contains the original environment, probe,
`observations/cache.csv`, `observations/depth.csv`, summary CSVs, and `iostat.txt`.
Committed observations are snapshots; later runs may differ.

## Environment and method

Release build, one userspace thread, Linux 7.0.0-1019-azure x86_64, ext4 on
`/dev/sda1`, scheduler `[none] mq-deadline`, 4096-byte pages, logical block 512
and physical block 4096 bytes. Model `Virtual Disk`, ROTA 1: physical media type
is **unknown**, not established as HDD. Full mount options are in the artifact;
notably they include `nobarrier` and `data=writeback`, not a durability setup.

`scripts/observe_storage.py` creates a disposable, fully populated 256 MiB file.
Before cold-labelled runs it fsyncs the file and requests file-scoped eviction
using `POSIX_FADV_DONTNEED`; `fincore` records actual residency rather than assuming
the advisory request worked. No global drop_caches command is used. This avoids
evicting unrelated files. A fresh VM block-backed test directory is required.

Cache runs are sequential 64 KiB reads, no warmup, 2-second measured phases.
Offsets wrap, so even the cold-start buffered run becomes predominantly warm;
this is **not a cold-storage throughput comparison**. UTC windows in cache.csv
include command output processing beyond measured I/O time.

## File residency plus device I/O

| Run | Resident pages before | Resident pages after | Guest partition read MiB |
|---|---:|---:|---:|
| buffered-cold | 0 | 65536 | 256.0000 |
| buffered-warm | 65536 | 65536 | 0.0000 |
| direct-cold | 0 | 0 | 442.1875 |
| direct-repeat | 0 | 0 | 443.5625 |

65536 pages equals the entire file. Warm buffered reads added no guest-device
read traffic in this window. Direct reads kept zero file pages resident while
issuing guest block reads, including the repeated run. Read MiB can exceed file
size because the workload wraps. Counters use the backing partition's sysfs
`stat` sector count (kernel accounting units are 512 bytes, independent of device
alignment); unrelated partition activity can contribute.

Selected `iostat -dx -t -y 1` samples for whole disk `sda`, 2026-11-20 UTC:

| Sample time | Phase | r/s | rkB/s | aqu-sz |
|---|---|---:|---:|---:|
| 11:04:44 | Buffered warm | 0.00 | 0.00 | 0.00 |
| 11:04:45 | Buffered warm | 0.00 | 0.00 | 0.00 |
| 11:04:48 | Direct cold-start | 3517.00 | 225088.00 | 0.97 |
| 11:04:49 | Direct repeat | 3459.00 | 221376.00 | 0.94 |

This combination demonstrates guest page-cache bypass on this tested ext4 path;
a successful O_DIRECT open alone would not. It says nothing about host or device
cache bypass. Device-wide iostat is supporting evidence, not per-file tracing.

## Queue depth changes useful concurrency

Same populated file, direct io_uring random 4 KiB reads, 1-second warmup and
2-second measurement. Three paired repetitions alternate depth order: 1/32,
32/1, then 1/32. Each measured row has submitted = completed = operations;
reported software-slot maxima are respectively 1 and 32.

| Repeat | Depth 1 IOPS | Depth 32 IOPS |
|---|---:|---:|
| 1 | 7012.210 | 19676.361 |
| 2 | 7127.553 | 19691.144 |
| 3 | 7029.985 | 19531.811 |

Calculated medians are 7029.985 and 19676.361 IOPS, a 2.80x
ratio **in this run**, not a general speedup guarantee or statistical validation.
An iostat depth-1 sample at 11:04:52 has aqu-sz 0.93; a depth-32 sample at
11:04:55 has aqu-sz 31.62. This supports increased guest block-layer concurrency,
not proof of physical-device queue occupancy. Sync engines remain depth one and
reject `--iodepth 32`; an asynchronous API alone does not guarantee higher speed.

## Reproduce

Install the [setup prerequisites](setup.md), build release, then use an existing,
empty scratch directory on an accepted filesystem:

```sh
python3 scripts/verify_engines.py ./build-release/iobench /mnt/test/scratch
TZ=UTC LC_ALL=C python3 scripts/observe_storage.py ./build-release/iobench /mnt/test/scratch
```

Outputs go under gitignored `results/`. The matrix keeps raw sample/summary CSV;
observation runs retain summary-only subsets, cache counts, device metadata, and
iostat. No speed threshold gates CI. File residency/count assertions are
functional checks, not performance assertions. See [limits](caveats.md).

Counter references: Linux [I/O statistics](https://www.kernel.org/doc/html/latest/admin-guide/iostats.html),
[POSIX_FADV_DONTNEED](https://man7.org/linux/man-pages/man2/posix_fadvise.2.html),
and [fincore](https://man7.org/linux/man-pages/man1/fincore.1.html).
