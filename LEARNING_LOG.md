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
