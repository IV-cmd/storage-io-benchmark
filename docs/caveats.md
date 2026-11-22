# Direct-I/O and io_uring caveats

## A successful open is not proof of cache bypass

`O_DIRECT` behavior depends on kernel and filesystem. An actual `open` failure is
reported using its errno; the tool never retries that open without `O_DIRECT`.
On the recorded Actions runner, both ext4 and tmpfs accepted the flag (errno 0).
Tmpfs reported no direct-I/O alignment. The tool explicitly rejects tmpfs and
OverlayFS even when open succeeds; it does not invent an EINVAL from the kernel.
The tmpfs refusal was exercised; OverlayFS refusal is present but was not run.

Direct engines require positive `statx(STATX_DIOALIGN)` values and the backing
device's logical block size from `/sys/dev/block/MAJOR:MINOR`, checking the
partition's parent when needed. STATX_DIOALIGN needs recent Linux headers/kernel
(available since Linux 6.1) and filesystem support. Missing alignment/sysfs data
is an error, not permission to assume 512 or silently use buffered I/O.

Filesystem memory and offset requirements need not be identical. The recorded
ext4 probe reported memory alignment 4 and offset alignment 512; the virtual
device reported logical 512 and physical 4096 bytes. Buffers satisfy both the
reported memory requirement and logical block size; offsets/lengths satisfy both
reported offset alignment and logical size. Physical size is not substituted for
logical size. `posix_memalign` allocates separate live slots; guards run before
measured synchronous calls or SQE preparation.

## Preparation and measurement are different

Only measured direct operations retain the direct-I/O contract. When a read
file needs extending, preparation temporarily clears O_DIRECT to fill only new
bytes, including a partial old tail, then restores it before warmup. Existing
contents are preserved. This is setup, not a measured buffered fallback. It can
leave dirty/cached data; initial direct reads may pay coherence/writeback costs.
The observation script fsyncs and evicts its file after preparation.

Existing sparse/unwritten regions are not detected. Such reads, compression,
deduplication, and storage/controller/provider caches can change physical work.
Direct I/O bypasses the guest file page cache, not every cache or the filesystem,
and does not imply persistence. Do not mix concurrent buffered/direct/mapped
access to the target. Raw device targets are rejected.

## Rings, errors, and instrumentation

The io_uring engine deliberately uses direct I/O. Liburing handles mapping and
memory ordering; raw ring syscalls would add code without improving this workload
comparison. There is no SQPOLL, IOPOLL, fixed-buffer registration, or application
thread pool. Containers/security policy can deny setup; that error is surfaced.

SQEs can finish out of order. A slot's buffer remains live until its CQE is
consumed; returned slots, transfer lengths, and exact counts are checked. Pending
work drains before buffer release. If shutdown cannot safely reap submitted
requests, the program terminates rather than free memory still in use.
Negative completions, partial submission failures, and shutdown errors were not
fault-injected. ASan/UBSan passed with no suppressions or separate unsanitized
test path; they do not instrument the kernel or the distro liburing binary.

## Virtual devices and observation tools

Runner backing devices vary: observed layouts included a virtual SCSI disk,
NVMe-named virtual storage, and `/dev/mapper/cow-root`. A device-mapper queue may
not expose `scheduler`; optional metadata is recorded as unavailable instead of
crashing or pretending a scheduler was selected. Mandatory engine alignment
queries still fail if unavailable. Multi-device/network filesystems are not
validated. ROTA/model names are not evidence of physical SSD/HDD media.

See [the observation note](cache-bypass.md). Guest device counters include other
activity, and a short shared-VM experiment is not a hardware performance claim.

References: Linux [open(2)](https://man7.org/linux/man-pages/man2/open.2.html),
[statx(2)](https://man7.org/linux/man-pages/man2/statx.2.html), and
[io_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html).

## Cached-read throughput includes benchmark overhead

When reads hit RAM, per-operation timing and bookkeeping can be a substantial
part of runtime. Matching block size and syscalls does not make two benchmark
loops equally expensive. A fixed-count comparison verified equal completed
work; reducing fio's timing/statistics work nearly removed the throughput gap
in that experiment. This is not permission to disable statistics for the
acceptance comparison or change its 10% tolerance.

fio `psync`, not `sync`, matches this tool's `pread` path. fio's slat/clat/lat
have different boundaries; absent synchronous slat samples do not mean zero
overhead. Our interval also includes wrappers and completion observation, not
just time inside the kernel. See [the measured reconciliation](validation.md#cached-read-investigation)
for raw evidence, commands, and the limitations of this comparison.
