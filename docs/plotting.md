# Reproduce the plots

Python 3.10+ is required; capture and CI use Python 3.12 on Ubuntu 24.04.
From the repository root:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
make plots
```

This regenerates seven PNGs in committed `docs/plots/` from committed
`docs/data/`. It does not run the benchmark or interpret an arbitrary CI
`results/matrix/` directory as performance data: that matrix contains tiny
functional checks, not timed performance trials.

## Inputs

`make plots PLOT_DATA=results/plot-data PLOT_DIR=docs/plots` accepts this layout:

```text
plot-data/
  throughput.csv
  depth.csv
  distribution-sync.csv.gz
  distribution-odirect.csv.gz
  distribution-iouring.csv.gz
  sources.json
  collect.py
  fio/
    comparison.csv
    settings.json
    environment.txt
    <workload>/tool-*.csv
    <workload>/fio-*.json
    <workload>/*command.txt
```

The first two CSVs concatenate benchmark **schema-2 summary rows** with a
single original header. No measurements are reconstructed from prose or rounded
tables. Distribution files preserve complete benchmark CSV output, compressed
without dropping samples. The existing [CSV contract](../README.md#csv-contract)
defines columns and units.

The plotter checks schema, completed-operation/byte/rate arithmetic, balanced
repetitions, matching workload conditions, complete throughput cells and CDF
sample counts. It independently checks sample mean/percentiles against their
summary. It refuses inconsistent input instead of smoothing or silently
discarding it.

| Output | Aggregation and axes |
|---|---|
| `throughput-{seq,rand}-{read,write}.png` | Median throughput per engine/block size; block size in KiB on a log axis, throughput in MiB/s. All application depths are one. |
| `queue-depth.png` | Separate throughput and mean/p50/p95/p99 latency panels versus application depth. Latency uses milliseconds and a log axis when positive. |
| `latency-cdf.png` | Exact empirical CDF from one fixed-operation run per engine. Equal latencies are counted together, not downsampled. Positive latencies use a log x-axis. |
| `fio-comparison.png` | All published comparison cells, including divergent buffered reads. Separate buffered/direct panels have explicitly different y-scales. Deltas are relative to fio. |

Lines join measured block sizes/depths; they are not fitted curves. Repetition
summaries are reduced independently: the median of per-run p99s is not the p99
of pooled samples. A CDF describes recorded operations, not a latency guarantee.
CI checks regeneration from preserved inputs, not performance regressions.
The fio CSV rounds rates and deltas; its arithmetic check allows that published
rounding, not a widened acceptance tolerance.

## Published curve conditions

`sources.json` retains the Actions run/benchmark-source commit, kernel,
filesystem/mount and virtual-device metadata, exact commands, repetition order,
and file residency before/after each trial. The collector is preserved beside
the data. The benchmark C++ sources were unchanged; the manually dispatched
`Collect presentation curves` workflow ran the collector and plotter.
The [published capture](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45324316546)
ran on Linux 7.0.0-1017-azure, ext4 on `/dev/sda1`, model `Virtual Disk`.
No physical-media type is inferred from that model or its ROTA flag.

The populated file is 256 MiB. Throughput trials use two measured seconds,
three repetitions, sequential/random reads/writes and 4/64/1024 KiB blocks.
The seven-depth random-read sweep uses five measured seconds and three
repetitions. The middle repetition reverses case order. Each distribution
retains 100,000 measured operations.

Before every trial the collector fsyncs and requests file-scoped eviction,
then checks residency with `fincore`. Buffered reads are explicitly preloaded;
direct reads start evicted. Warmup is disabled. This intentionally contrasts
cache paths, not equal amounts of device work. Writes repeatedly overwrite the
same range, omit fsync from timing, and measure acceptance rather than durability.
Capture stays in tmpfs until collection finishes. No readahead or scheduler
setting is changed by this collector.

The buffered sequential-read curve is non-monotonic: its medians are
13,533.2 MiB/s at 64 KiB and 2,614.6 MiB/s at 1024 KiB. All three repetitions
are retained. File residency was checked, but this capture does not isolate
the cause of that drop or establish an optimal block size. No values were
smoothed or replaced to make the curve look expected.

The fio panel is deliberately **a separate experiment**:
[the retained three-pair run](https://github.com/IV-cmd/storage-io-benchmark/actions/runs/45015819653),
with a 1 GiB file, five measured seconds, one-second warmup and alternating tool
order. Its original summaries, fio JSON, commands, environment and settings are
committed under `fio/`; its benchmark-source commit is in `fio/settings.json`.
Four cells pass the unchanged ±10% throughput tolerance; two buffered cells
fail. Do not combine this host's rates with the new curves into a paired claim.

## Collect new curves

Use a release build and an otherwise idle block-backed Linux filesystem.
The collector also needs `fincore`; Ubuntu 24.04 ships it in `util-linux-extra`.
Choose an existing writable scratch directory; the collector creates and removes
its own disposable regular file. Never pass a raw device or valuable file.

```sh
make release
python3 docs/data/collect.py ./build-release/iobench /mnt/test results/plot-data
cp -R docs/data/fio results/plot-data/
make plots PLOT_DATA=results/plot-data
```

The copy explicitly retains the old fio experiment; it is not a fresh
cross-validation result. For a new comparison run
`make validate SCRATCH=/mnt/test` and use that timestamped bundle as the input
`fio/` directory instead. Exit status 3 means throughput tolerance misses, not
a parser or benchmark crash.

Do not compare curves from different machines/cache policies without preserving
their metadata. These guest-path observations do not classify host media or
identify an isolated device bottleneck. See [analysis](analysis.md),
[tuning](tuning.md), [validation](validation.md) and [caveats](caveats.md).
