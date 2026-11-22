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
This stage did not yet include fio or perf/blktrace; see the next section for
those additions. Plots, fault injection, and physical-media validation remain
outside the implemented scope.

1. Why did successful O_DIRECT open on tmpfs prove nothing about bypass?
2. What breaks if a buffer is reused before its completion arrives?
3. Why can a lost completion make throughput look plausible, and what invariant prevents it?

## fio comparisons and path instrumentation

### What changed

`make validate` compares six read cells with fio, using three alternating pairs
and median throughput. The 10% tolerance is unchanged. Each bundle records its
commit, settings, environment, exact commands, and raw CSV/JSON. The manual
[repeated workflow](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/validate.yml)
publishes evidence even when the comparison fails.

`make observe` captures `iostat`, `blktrace`/`btt`, and four explicit perf
software events. Capture uses `/dev/shm`; results reach the measured filesystem
only after monitors stop. Missing tools/permissions remain explicit rather
than invented measurements.

### What broke, and what fixed it

| Finding | Correction / lesson |
|---|---|
| Green main CI concealed fio tolerance misses behind `continue-on-error`. | Exit 3 identifies a tolerance miss; main CI warns and publishes it, while other errors fail. The repeated workflow fails on misses. |
| The earlier AWS all-pass table referenced only a VM directory. | Replaced the published baseline with a GitHub run whose raw files, commands, commit, and environment can be inspected. |
| fio was described as submitting larger batches without setting that option. | Set submission/completion batches explicitly to one; matching depth does not make implementations identical. |
| Default perf events were unsupported and did not establish context-switch counts. | Request task-clock, context-switches, cpu-migrations, and page-faults explicitly; probe support before collecting. |
| The runner's unprivileged perf probe was blocked by `perf_event_paranoid=4`. | Collect as root on the disposable CI runner, record UID, and leave kernel policy unchanged. Other hosts can still report unavailable. |
| Root collection copied private temporary-directory permissions into artifacts, blocking upload. | Restore runner ownership on step exit, including failure, before the artifact action reads results. |
| Small Q2G and dominant D2C were interpreted as proving a device bottleneck. | Q2G covers allocation only; D2C includes driver/device work. Neither rules out other kernel costs or isolates physical-media latency. |
| Timing differences were attributed to fio overhead or VM noise without a controlled experiment. | Keep those as hypotheses. Repeated buffered misses remain unresolved, not explained away. |

### What actually ran

[Repeated run 45015819653](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45015819653)
used Linux 7.0.0-1020-azure, fio 3.41, ext4 on virtual `sda1`, a populated 1 GiB
file, five-second measurements, one-second warmups, and three alternating pairs.
Four direct-I/O cells pass; buffered sequential/random 4 KiB reads miss by
+23.3%/+20.5%. Exit 3 correctly makes that workflow red. The comparison was
recalculated from the raw repeats, including completed/submitted operation
counts. Full results and commands are linked from [validation](docs/validation.md).

[Observation run 45017058458](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45017058458)
published all four perf software counters for all three observation workloads.
These are whole-process totals, including warmup and output; they must not be
divided by only the measured duration. Block traces include all device traffic,
including unrelated processes. No per-engine context-switch rate or isolated
device-service-time claim is made from these captures.

### Limits to explain back

The implementation and evidence collection exist, but all-six-cell fio
agreement is not established. Writes, other kernels/filesystems, durability,
and physical-media properties are not validated. Trace instrumentation changes
the workload, and matching random distributions does not mean matching offsets.

1. Why can main CI be green while the repeated fio workflow correctly fails?
2. Why does near-zero Q2G not prove the kernel has no other bottleneck?
3. Why would dividing whole-process context switches by the two-second measured window be misleading?

### Follow-up: isolate cached-read measurement overhead

Inspecting the existing fio JSON ruled out lat versus clat alone: their buffered
mean difference was only about 26–28 ns, and the gate measures throughput.
Changing fio from psync to sync would also change pread to read, not improve
workload matching.

A [fixed-count experiment](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45019464171)
verified two million completed reads per tool/variant. Normal fio showed
+11.9%/+11.4% tool throughput deltas; diagnostic `gtod_reduce=1` reduced them to
+0.4%/+0.1%. File-filtered strace separately verified ten thousand pread64 calls
from each tool. Instrumentation therefore matters in this cached-read run,
but disabling it cannot validate latency or replace the original acceptance
comparison. The 10% throughput gate is unchanged. See the
[method and raw evidence](docs/validation.md#cached-read-investigation).
