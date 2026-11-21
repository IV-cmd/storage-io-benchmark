# Build and development setup

Linux with CMake 3.20+, a C++20 compiler, Git, Make, and Ninja is required.
Clang 18 supplies the pinned formatter, linter, and sanitizer-build compiler.
On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y git make g++ cmake ninja-build clang-18 clang-format-18 clang-tidy-18 liburing-dev sysstat util-linux
```

`make validate` and `make observe` additionally need `fio`, `blktrace`
(also provides `blkparse`/`btt`), and a `perf` matching the running kernel:

```sh
sudo apt-get install -y fio blktrace linux-tools-$(uname -r)
```

From a fresh clone, `make build` configures and builds the project in one
command. The first configuration fetches GoogleTest v1.15.2, so it needs network
access. The wrapper uses these CMake/Ninja commands:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
./build/iobench --help
```

## Tests and tools

```sh
make test          # six focused math/offset/engine checks
make format        # format only our C++ files
make format-check  # reject formatting differences
make tidy          # curated checks against compile_commands.json
make test-asan     # separate Clang build with ASan and UBSan
```

ASan detects memory-access errors; UBSan detects instrumented undefined
behavior. Sanitizer errors stop execution. These checks are not performance
measurements. Do not use sanitizer/debug builds for future benchmark results.
All engines perform real file I/O. Direct engines also require kernel/filesystem
DIO alignment reporting and accessible device sysfs data. Use disposable files and see the
[usage and CSV contract](../README.md). Release builds exclude sanitizers:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel 2
```

## Measurement environment requirement

Workload measurements must use a regular test file on a mounted **real VM block
device**, not tmpfs or Docker OverlayFS. Use disposable test files, not a raw
device path or valuable data. Direct engines reject tmpfs/OverlayFS; buffered mode does not enforce filesystem
type. Read setup needs write access to
create/extend and initialize new file regions; writes overwrite existing bytes.

Tmpfs is RAM-backed, and some kernel/filesystem combinations reject O_DIRECT
with EINVAL; record the actual behavior rather than assuming it. OverlayFS can
add copy-up work and change the I/O path. Neither is an accepted measurement
filesystem for this project.

Before measuring, record the host's actual kernel, device, filesystem/mount
options, and selected I/O scheduler, for example:

```sh
uname -r
findmnt -T /path/to/test-directory -o SOURCE,FSTYPE,OPTIONS
lsblk -o NAME,TYPE,SIZE,ROTA,MODEL,MOUNTPOINTS
cat /sys/block/DEVICE/queue/scheduler
```

Replace the example directory and DEVICE with the benchmark file's actual
backing mount and device. A virtual disk's ROTA flag alone does not establish
SSD versus HDD: document the provider's volume type or hardware evidence.
No SSD/HDD environment or performance result has been validated yet. GitHub
Actions checks functionality and records exploratory cache/depth observations,
not validated storage performance.

Liburing headers/library are required for every build. Cache observations also
use Python 3, `iostat` (`sysstat`), and `fincore` (`util-linux`). CI installs
liburing/sysstat and uses the runner's Python/util-linux. No sanitizer
suppressions are needed. Plotting (pandas/matplotlib) is not part of these checks.

For GitHub-only editing, manual CI accepts an optional `sources` JSON map of
proposed C++ files. Actions formats/checks those files without committing; after
the run passes, `formatted-candidate` contains the exact formatted sources.
Normal push/manual runs do not automatically cancel one another.

## fio validation and I/O-path observation

Both targets build `build-release/` and need an existing directory on the
block-backed filesystem under test. They create and delete a 1 GiB scratch file
and write a timestamped bundle under gitignored `results/`. Re-running is safe.

```sh
make validate SCRATCH=/mnt/test/scratch   # iobench vs fio; ~30 minutes by default
make observe SCRATCH=/mnt/test/scratch    # iostat + blktrace/btt + perf stat
```

Linux-only dependencies and permissions, all optional for `make observe`
(missing ones are recorded as `unavailable: <reason>` instead of failing):

| Tool | Package | Requirement |
|---|---|---|
| `iostat` | sysstat | Unprivileged; device names follow the kernel (`vda`, `nvme0n1`, `dm-0`). |
| `blktrace`, `blkparse`, `btt` | blktrace | Root or `CAP_SYS_ADMIN`, plus debugfs at `/sys/kernel/debug`. Run as root or allow passwordless `sudo`; the script uses `sudo -n` and never prompts. |
| `perf stat` | linux-tools for the running kernel | `kernel.perf_event_paranoid` <= 2 for your own process (Ubuntu defaults to 4), or root. Temporarily: `sudo sysctl kernel.perf_event_paranoid=1`. VMs may report hardware counters as `<not supported>`. |

Traces are written to `/dev/shm` so trace files do not add I/O to the traced
disk. See [validation](validation.md) for tolerances and observed deltas.

See [direct-I/O caveats](caveats.md) and [observation reproduction](cache-bypass.md).
