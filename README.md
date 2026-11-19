# Storage I/O Benchmark & Linux I/O Path Analysis (C++)

[![CI](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/IV-cmd/storage-io-benchmark/actions/workflows/ci.yml)

A standalone C++20/Linux learning project for studying storage I/O. **Skeleton
only:** the CLI prints configuration without opening files or devices. The
repository has a build system, development tools, and one placeholder test;
it has no benchmark engines, statistics implementation, or measured results.

## Build and inspect

Install the prerequisites in the [setup guide](docs/setup.md), then build with
one command:

```sh
make build
./build/iobench --help
./build/iobench --engine pread --block-size 4k --queue-depth 1 --file example.dat --runtime 10
```

`make test` runs the placeholder, not a storage correctness check. CI builds,
checks formatting, runs curated clang-tidy checks, and rebuilds/runs the test
under AddressSanitizer and UndefinedBehaviorSanitizer.

The pure statistics API is declared in `src/common/stats.h` but not implemented.
Future work includes pread/pwrite, direct I/O, io_uring, fio comparisons,
kernel-side analysis, and plots. None is claimed as complete.

MIT licensed; see [LICENSE](LICENSE).
