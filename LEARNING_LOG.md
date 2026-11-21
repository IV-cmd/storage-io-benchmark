# Learning log

## Direct I/O, async requests, and honest measurements

### What changed

`Engine::Run()` remains the public workload boundary. Internal `Io::Submit`,
`Flush`, and `Complete` let the same runner use synchronous or asynchronous
requests without engine branches in runner/statistics code. Liburing was chosen
to keep mapping/barrier details out of the benchmark. Both new engines use
O_DIRECT; only io_uring permits multiple outstanding request slots.

### What broke, and what fixed it

| Finding | Correction / lesson |
|---|---|
| Repeated hand-formatting mistakes stopped CI before runtime checks. | Applied actual clang-format output; added Actions preflight for proposed C++ edits before committing. |
| Huge clang-tidy counts looked like project errors. | Completed logs identify suppressed non-user diagnostics; counts are cumulative, not a failure verdict. |
| Manual dispatch canceled a push check; the first fix still let pushes cancel older pushes. | Removed automatic concurrency cancellation entirely and waited for checks before further commits. This was a workflow mistake, not a C++ failure. |
| A later runner used device-mapper and had no queue/scheduler file. | Record absent optional metadata honestly; keep mandatory engine alignment checks unchanged. Earlier assumptions about a fixed runner layout were wrong. |
| Filesystem memory alignment and device logical size differed. | Query both and satisfy their combined requirements for buffer allocation, offset, and length. Do not assume physical size equals logical size. |
| Tmpfs accepted O_DIRECT with errno 0 and reported no DIO alignment. | Reject it explicitly; do not fabricate a kernel EINVAL or label it direct measurement. |

### What actually ran

[Run 21](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/44724286571)
passed formatting, clang-tidy, six tests under ASan/UBSan, and the release matrix.
The two added tests check alignment failure before a syscall and exact io_uring
counts for 7 and 77 operations at depth 32, including warmup discard and partial
queues. There are no sanitizer suppressions or unsanitized test substitutions.

The release matrix has 48 runs: sync/1, odirect/1, iouring/1, iouring/32; each
uses sequential/random reads/writes with 4 KiB, 64 KiB, and 1 MiB blocks. Each
CSV has exactly 37 measured samples and matching submitted/completed counts;
post-run bytes match the fixed write pattern. Expected misalignment, sync-depth,
and tmpfs errors were exercised. No ring-full or lost-CQE bug was observed;
partial-submit/error/shutdown paths were not fault-injected.

### Observed cache and depth behavior

The [analysis note](docs/cache-bypass.md) links original evidence. Buffered reads
populated all 65536 pages of a 256 MiB file; warm reads caused zero measured
partition read MiB. Direct cold-start and repeated reads retained zero pages and
caused 442.1875 / 443.5625
partition read MiB. These are guest cache/device observations, not SSD/HDD claims.

Depth-1 random-read IOPS were 7012.210, 7127.553, 7029.985.
Depth-32 values were 19676.361, 19691.144, 19531.811.
The median ratio was 2.80x in that run. Order alternation reduces one bias,
but three short pairs on a variable VM do not establish general performance.

### Limits to explain back

Direct I/O is not durability. io_uring is not automatically direct or syscall-free;
this implementation chooses direct mode. A slot cannot be reused until its CQE
is consumed, and completion order need not match submission order. Reported
latency includes queueing and userspace reaping delay. Schema 2 records engine,
depth, and summary accounting; old schema-1 datasets must be handled explicitly.
No fio, perf/blktrace, plots, fault injection, or physical-media validation yet.

1. Why did successful O_DIRECT open on tmpfs prove nothing about bypass?
2. What breaks if a buffer is reused before its completion arrives?
3. Why can a lost completion make throughput look plausible, and what invariant prevents it?

## fio validation and trace instrumentation (M3)

### What changed

`make validate` (`scripts/validate_fio.py`) runs six read cells through both
iobench and fio 3.28 — three alternated pairs, medians compared, `results/`
holds raw CSV/JSON. `make observe` (`scripts/observe_run.py`) wraps three
workloads with `iostat -dxy` on the backing disk, `blktrace` + `btt` written to
`/dev/shm`, and `perf stat`; missing tools or permissions are recorded as
`unavailable: <reason>` instead of failing. Tolerance is 10% throughput,
fixed before the first run; latency is reported, not gated.

### What broke, and what fixed it

| Finding | Correction / lesson |
|---|---|
| Send `SIGINT` to `sudo blktrace` to stop it — the script hung 7 minutes; sudo did not relay the signal and blktrace kept tracing. | Give `blktrace -w <seconds>` so it exits by itself; monitor shutdown now escalates SIGINT → SIGTERM → SIGKILL with timeouts. A signal sent to a launcher is not a signal sent to the tool. |
| Ubuntu 22.04 kernel headers predate `statx` DIO fields; the release build failed on this VM though CI is fine on ubuntu-latest. | Built with Linux 6.8 UAPI headers plus liburing 2.5 built from source in `~/liburing`; the compiled binary is normal and this is a dev-environment detail, not a code fix. |
| Block devices report `major 0` on virtual filesystems — the tool rejected tmpfs/OverlayFS anyway; observe script also resolves the backing disk via `/sys/dev/block`. | Reused the same resolution; partitions map to their parent disk (`vda` for `vda1`). |
| Running both tools niced (background job) still produced clean medians. | Noise affects both tools symmetrically; alternating order matters more than niceness for a delta comparison. |

### Observed deltas (AWS VM, ext4 on virtio vda, 1 GiB file, 5s+1s, 3 repeats)

All six cells passed: buffered seq/rand 4k +5.9%/+7.4%, O_DIRECT seq/rand 4k
−2.6%/−7.2%, O_DIRECT rand 64k −0.4%, io_uring depth-32 rand 4k −0.4%.
Buffered cells trend a few percent high (fio's per-op accounting at µs
latencies); O_DIRECT trends slightly low. Full table and method mapping in
[docs/validation.md](docs/validation.md). The earlier smoke run at 1-second
durations showed ±20–40% swings purely from short-window VM noise — evidence
for keeping the gate at medians of alternated pairs, not single runs.

### Traces confirmed the block-layer story

`btt` on the io_uring depth-32 run shows ~460k I/Os, Q2G ~0.26µs avg, D2C
~143µs avg — nearly all time is device latency, not queueing overhead. `perf
stat` on O_DIRECT rand 4k shows ~165k context switches/sec (one per ~6µs
synchronous read) vs ~30/sec for a warm buffered run; hardware counters are
`<not supported>` on this hypervisor, so instructions/cycles are absent.

### Limits to explain back

The 10% tolerance was fixed before measuring, but validation is read-only and
single-host: nothing is proven about writes, other kernels, or other hardware.
fio's batched io_uring submission and histogram percentiles are not identical
measurements — agreement on throughput is the contract, not identical latency
shapes. Trace capture itself perturbs the run (blktrace adds per-I/O work and
iostat reads sysfs), so observed magnitudes are context, not isolation.

1. fio `--iodepth=32` submits SQEs in batches while iobench submits one per reaped CQE — why does throughput still match, and which path would divergence show up in first?
2. Why does `Q2G` near-zero with `D2C` dominant tell you the kernel is not the bottleneck — and what would large `Q2G` mean instead?
3. perf shows ~165k context switches/sec for sync rand-4k but ~30/sec for buffered seq-64k. Explain both numbers from what each engine does per I/O.
