# Build and development setup

Linux with CMake 3.20+, a C++20 compiler, Git, Make, and Ninja is required.
Clang 18 supplies the pinned formatter, linter, and sanitizer-build compiler.
On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y git make g++ cmake ninja-build clang-18 clang-format-18 clang-tidy-18
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
make test          # four focused math/offset/engine checks
make format        # format only our C++ files
make format-check  # reject formatting differences
make tidy          # curated checks against compile_commands.json
make test-asan     # separate Clang build with ASan and UBSan
```

ASan detects memory-access errors; UBSan detects instrumented undefined
behavior. Sanitizer errors stop execution. These checks are not performance
measurements. Do not use sanitizer/debug builds for future benchmark results.
The buffered engine performs real file I/O. Use disposable files and see the
[usage and CSV contract](../README.md). Release builds exclude sanitizers:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel 2
```

## Measurement environment requirement

Workload measurements must use a regular test file on a mounted **real VM block
device**, not tmpfs or Docker OverlayFS. Use disposable test files, not a raw
device path or valuable data. The CLI does not yet enforce filesystem type. Read setup needs write access to
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
Actions verifies the toolchain, not storage performance.

Later analysis needs `fio`, `iostat` (the `sysstat` package), a kernel-compatible
`perf`, `blktrace`/`blkparse`, and Python 3 with pandas/matplotlib. They are not
needed for buffered engine smoke checks and are not installed by CI.
