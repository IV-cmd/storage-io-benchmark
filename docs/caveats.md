# I/O measurement caveats

This separates **encountered traps** from things not tested. It does not invent
kernel errors or physical-media explanations. The [analysis](analysis.md) and
[validation](validation.md) preserve measured values and raw artifact locations.

## Filesystems: a successful O_DIRECT open proves little

In [run 45236536614](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45236536614),
`engine-verification/host-probe.txt` records successful O_DIRECT opens on both
ext4 and tmpfs: **errno 0**, not the anticipated tmpfs `EINVAL`. Tmpfs returned
zero direct-I/O alignment values. The benchmark's `io-path-analysis/tmpfs-probe.txt`
then records **exit 2**, explicitly refusing that filesystem without retrying
buffered. That application refusal is not a kernel open error. Actual open
failures retain their errno; successful open alone cannot establish bypass.

The same run mounted a scratch OverlayFS over ext4. Its
`io-path-analysis/overlay-probe.txt` records a one-operation buffered 4 KiB read
succeeding, followed by a direct attempt that opened successfully but refused
OverlayFS with exit 2. After both probes, `upper_copy_exists=True`. Both paths
open read/write even for reads, which can trigger copy-up; the probe did not
sample between opens to attribute it to one specifically. The upper copy shows
read-mode setup can change the layered path, **not a measured OverlayFS slowdown**.
Both direct engines explicitly reject tmpfs/OverlayFS; buffered mode still
requires the operator to check the mount.

## Alignment: filesystem, logical and physical sizes differed

That run reported ext4 `STATX_DIOALIGN` memory/offset requirements **4/512 bytes**,
while the virtual disk reported logical/physical sizes **512/4096 bytes**.
These are not interchangeable. Buffers satisfy filesystem memory alignment and
logical block size; offsets and lengths satisfy filesystem offset alignment and
logical size. `posix_memalign` supplies live, separate request-slot buffers.
The [alignment tests](../tests/core_test.cc) exercise rejection before I/O.

The [direct path](../src/bench/direct.cc) requires positive `STATX_DIOALIGN` and
backing-device logical-size metadata, following a partition to its parent when
needed. Missing mandatory metadata is an error, not a reason to assume 512 or
fall back. Header/kernel/filesystem support therefore matters; a successful
build alone is not a measurement-environment check.

## Cache state and preparation changed what was measured

The [earlier cache experiment](cache-bypass.md) left **65,536** file pages resident
after buffered reads and **zero** after direct reads. Its cold-start buffered
duration run wrapped and warmed the file: "started cold" did not mean every
operation reached storage. The new [single-pass readahead comparison](analysis.md#readahead-cold-sequential-scan-versus-sparse-random-reads)
avoids that wrap, verifies eviction and still allows prefetch to warm later pages.
File-scoped eviction is advisory, so `fincore` verification is part of the evidence.
Neither experiment evicted provider/controller caches.

Read preparation may fill new file bytes using buffered I/O **outside timing**,
then restore the direct flag. That is not a measured fallback, but setup can
leave cached or dirty data. Observation collectors fsync/evict before trials.
Existing sparse/unwritten regions are not detected by the benchmark; use a
known populated scratch file. Existing contents are preserved and larger files
are not shrunk. No concurrent buffered/direct/mapped access was tested.

## Measurement work matters at cached-read speeds

The [fixed-count fio diagnostic](validation.md#cached-read-investigation) reduced
sequential/random throughput gaps from **11.9%/11.4%** to **0.4%/0.1%** by reducing
fio timing/statistics work. That demonstrates instrumentation sensitivity in
that run; it does not fully explain the earlier **23.3%/20.5%** discrepancies.
The 10% throughput tolerance is unchanged, and reduced-statistics mode is not
an acceptance baseline or latency validation. Switching lat to clat cannot fix
a throughput verdict. Synchronous slat had no samples, not proof of zero overhead.
fio psync matches `pread`; fio sync changes the syscall to `read`.

Both tools' intervals include userspace work, not isolated kernel service time.
Our runner also records samples and includes final draining in elapsed time.
Samples consume memory proportional to completed operations; output/statistics
are outside phase timing but inside whole-process perf totals.

## Virtual storage and tunables: only the exposed guest path was measured

Published runners exposed virtual SCSI, NVMe-labelled virtual storage and
`/dev/mapper/cow-root`. ROTA=1 and model strings do not establish HDD/SSD media;
NVMe naming does not prove bare-metal NVMe. No virtio-specific or bare-metal
comparison was performed. Host/provider caches, limits and scheduling were not
isolated, so a guest plateau is not a physical-device specification.

[Run 44723779679](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/44723779679)
failed because the device-mapper layer exposed no scheduler file. Optional
metadata now records unavailable; it never invents a selection. The new tuning
run used a non-mapped device with `none mq-deadline`, not BFQ. It does not
re-exercise every mapped-device path. Readahead's `blockdev` units are 512-byte
sectors; **256 sectors meant 128 KiB**, not 256 KiB. Scheduler/readahead changes
affect the device, not just our file. The collector used a disposable VM and
verified restoration; copying those changes onto a shared host is not advised.

## Observation permissions, counter meanings and capture scope

The unprivileged perf capture in
[run 45016292082](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45016292082)
was unavailable under runner policy. Later captures used root on disposable
runners without lowering that policy. The new hardware-event-pair probe failed:
`cache-references` was unsupported; no hardware cache-miss number is reported.
Software events worked. Perf page faults count process virtual-memory faults,
not file-cache misses; hardware cache events describe CPU caches, not the Linux
file-page cache. Whole-process counters include setup, warmup, statistics and
output: do not divide them by the measured phase alone.

Published block traces used privileged collection. The script needs blktrace,
blkparse/btt and access to the device's tracing interface; missing tools or
permissions are recorded as unavailable, not an empty successful trace. No
unprivileged blktrace success is claimed. `iostat` and blktrace cover the whole
device, including unrelated and metadata traffic. A warm-read trial can have
zero reads but still show writes. Q2G measures request allocation; D2C includes
driver/device completion, not physical-media service time. [Trace examples](analysis.md#what-perf-and-the-block-trace-establish).

Captures are staged in `/dev/shm` to avoid writing trace files onto the device
being observed. Root-created private directory modes once caused
[`EACCES` during artifact upload](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45016704401).
The workflow restores runner ownership before upload, including on failure.
Tracing still perturbs execution; tmpfs staging does not remove tracing overhead.
Actions artifacts have retention limits: download evidence before expiry.

## Durability and untested failure boundaries

The new buffered-write trials reported **10,235.1 MiB/s** accepted with **zero
interior device writes**. Rewriting a cache-fitting range is not persistent
bandwidth. Neither buffered completion nor O_DIRECT implies durability: no
fsync/O_SYNC lies inside benchmark timing. The tested mount included
`nobarrier,data=writeback`; no power-loss behavior was tested. Write patterns are
fixed/compressible; compression/deduplication/provider-cache effects were not
separately measured. Writes lack fio cross-validation.

These remaining limits are **not claimed encountered failures**: injected
negative CQEs, partial submissions and shutdown errors; long mixed workloads;
multi-device/network filesystems; crashes or power loss. The direct io_uring
engine has no SQPOLL/IOPOLL, fixed-buffer registration or userspace thread pool.
It retains buffers until completion and checks transfer lengths and counts;
unsafe shutdown terminates rather than free live buffers. ASan/UBSan passed
without suppressions, but do not instrument the kernel or distro liburing.
Those boundaries follow the [code](../src/bench/iouring.cc), not a claim of fault
injection or production certification.
