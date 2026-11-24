"""Plot schema-2 benchmark CSVs; see docs/plotting.md for the input layout."""

import argparse
from itertools import product
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ENGINES = {"sync": "Buffered", "odirect": "O_DIRECT", "iouring": "io_uring + O_DIRECT"}
CONFIG = ["engine", "iodepth", "op", "block_size", "file_size", "pattern", "warmup_ns"]
LATENCY = ["mean_us", "p50_us", "p95_us", "p99_us"]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def summaries(path):
    data = pd.read_csv(path)
    required = CONFIG + LATENCY + ["schema_version", "record_type", "operations", "bytes",
                                 "elapsed_ns", "submitted", "completed", "mib_per_s", "iops"]
    require(set(required) <= set(data), f"{path}: missing CSV columns")
    require(data.schema_version.eq(2).all(), f"{path}: expected schema 2")
    require(data.record_type.isin(["sample", "summary"]).all(), f"{path}: unknown record type")
    rows = data[data.record_type == "summary"].copy()
    require(not rows.empty, f"{path}: no summaries")
    require(rows[CONFIG].notna().all().all(), f"{path}: missing configuration")
    numeric = rows[LATENCY + ["operations", "bytes", "elapsed_ns", "mib_per_s", "iops"]]
    require(np.isfinite(numeric).all().all() and numeric.ge(0).all().all(), f"{path}: invalid values")
    require(rows.operations.gt(0).all() and rows.elapsed_ns.gt(0).all(), f"{path}: empty measurement")
    require(rows.operations.eq(rows.submitted).all() and rows.operations.eq(rows.completed).all(),
            f"{path}: incomplete I/O accounting")
    require(rows.bytes.eq(rows.operations * rows.block_size).all(), f"{path}: byte-count mismatch")
    require(np.allclose(rows.mib_per_s, rows.bytes / rows.elapsed_ns * 1e9 / 2**20,
                        rtol=1e-6, atol=1e-5), f"{path}: throughput arithmetic mismatch")
    require(rows.p50_us.le(rows.p95_us).all() and rows.p95_us.le(rows.p99_us).all(),
            f"{path}: unordered percentiles")
    return data, rows


def fixed(rows, columns):
    require(all(rows[column].nunique() == 1 for column in columns),
            f"mixed measurement conditions: {columns}")


def save(fig, output, name, note):
    fig.tight_layout(rect=(0, 0.09, 1, 0.96))
    fig.text(0.02, 0.02, note, fontsize=8)
    fig.savefig(output / name, dpi=150)
    plt.close(fig)
    print(output / name)


def throughput(root, output):
    _, rows = summaries(root / "throughput.csv")
    fixed(rows, ["file_size", "warmup_ns", "iodepth"])
    require(rows.iodepth.eq(1).all(), "throughput curves require depth 1 for every engine")
    keys = ["engine", "pattern", "op", "block_size"]
    counts = rows.groupby(keys).size()
    blocks = sorted(rows.block_size.unique())
    expected = set(product(ENGINES, ["seq", "rand"], ["read", "write"], blocks))
    require(len(blocks) >= 2 and set(counts.index) == expected and counts.nunique() == 1,
            "throughput matrix is incomplete or repetition counts differ")
    median = rows.groupby(keys).mib_per_s.median()
    for pattern, operation in product(["seq", "rand"], ["read", "write"]):
        fig, ax = plt.subplots(figsize=(8, 4.8))
        for engine, label in ENGINES.items():
            values = median.loc[engine, pattern, operation].reindex(blocks)
            ax.plot(np.array(blocks) / 1024, values, "o-", label=label)
        ax.set_xscale("log", base=2)
        ax.set_xticks(np.array(blocks) / 1024, [f"{b / 1024:g}" for b in blocks])
        ax.set(xlabel="Block size (KiB; logarithmic scale)", ylabel="Throughput (MiB/s)", ylim=(0, None),
               title=f"{'Sequential' if pattern == 'seq' else 'Random'} {operation} — depth 1")
        ax.legend()
        ax.grid(alpha=0.25)
        conditions = "Buffered reads start warm; direct reads bypass the guest file cache." if operation == "read" else "Accepted bytes; no fsync in timing, not persistent-device bandwidth."
        save(fig, output, f"throughput-{pattern}-{operation}.png",
             f"throughput.csv; medians of {counts.iloc[0]} runs/cell. {conditions}")


def depth(root, output):
    _, rows = summaries(root / "depth.csv")
    fixed(rows, ["engine", "op", "pattern", "block_size", "file_size", "warmup_ns"])
    require(rows.engine.eq("iouring").all() and rows.op.eq("read").all() and rows.pattern.eq("rand").all(),
            "depth sweep requires io_uring random reads")
    counts = rows.groupby("iodepth").size()
    require(len(counts) >= 2 and counts.nunique() == 1, "incomplete depth repetitions")
    median = rows.groupby("iodepth")[["mib_per_s"] + LATENCY].median()
    fig, axes = plt.subplots(2, 1, sharex=True, figsize=(8, 7))
    axes[0].plot(median.index, median.mib_per_s, "o-", label="Throughput")
    axes[0].set(ylabel="Throughput (MiB/s)", ylim=(0, None))
    for column in LATENCY:
        axes[1].plot(median.index, median[column] / 1000, "o-", label=column.removesuffix("_us"))
    axes[1].set(xlabel="Application queue depth (requests; logarithmic scale)", ylabel="Latency (ms)")
    if median[LATENCY].gt(0).all().all():
        axes[1].set_yscale("log")
        axes[1].set_ylabel("Latency (ms; logarithmic scale)")
    for ax in axes:
        ax.set_xscale("log", base=2)
        ax.grid(alpha=0.25)
        ax.legend()
    axes[1].set_xticks(median.index, [str(d) for d in median.index])
    fig.suptitle(f"Direct io_uring random reads — {rows.block_size.iloc[0] / 1024:g} KiB")
    save(fig, output, "queue-depth.png",
         f"depth.csv; medians of {counts.iloc[0]} per-run summaries, not pooled percentiles.\n"
         "Application submit-to-observed-completion latency; no physical-device bottleneck attribution.")


def distribution(root, output):
    fig, ax = plt.subplots(figsize=(8, 4.8))
    configs, sizes, minima = [], [], []
    for engine, label in ENGINES.items():
        data, rows = summaries(root / f"distribution-{engine}.csv.gz")
        require(len(rows) == 1 and rows.engine.iloc[0] == engine, "expected one matching distribution run")
        row = rows.iloc[0]
        samples = data[data.record_type == "sample"]
        require(set(CONFIG + ["seq", "latency_ns"]) <= set(samples), "missing sample columns")
        values = samples.latency_ns / 1000
        require(len(values) == row.operations and np.isfinite(values).all() and values.ge(0).all(),
                "distribution sample count or latencies invalid")
        require(np.array_equal(samples.seq, np.arange(len(samples))), "non-contiguous sample sequence")
        require(all(samples[column].eq(row[column]).all() for column in CONFIG), "sample configuration mismatch")
        actual = [values.mean()] + [values.quantile(q) for q in [0.5, 0.95, 0.99]]
        require(np.allclose(actual, row[LATENCY].astype(float), rtol=1e-6, atol=1e-5),
                "sample latencies disagree with summary")
        cumulative = values.value_counts().sort_index().cumsum() / len(values) * 100
        ax.step(cumulative.index, cumulative.values, where="post", label=label)
        configs.append(row[[c for c in CONFIG if c != "engine"]])
        sizes.append(len(values))
        minima.append(values.min())
    config = pd.DataFrame(configs)
    fixed(config, list(config))
    if min(minima) > 0:
        ax.set_xscale("log")
    ax.set(xlabel="Operation latency (µs; log scale when strictly positive)",
           ylabel="Completed operations at or below latency (%)", ylim=(0, 100),
           title=f"Empirical latency CDF — {config.pattern.iloc[0]} {config.op.iloc[0]}, "
                 f"{config.block_size.iloc[0] / 1024:g} KiB, depth {config.iodepth.iloc[0]}")
    ax.grid(alpha=0.25)
    ax.legend()
    save(fig, output, "latency-cdf.png",
         f"distribution-*.csv.gz; {', '.join(map(str, sizes))} samples (legend order).\n"
         "Exact empirical CDF; no outlier removal, smoothing, or pooling across repetitions.")


def fio(root, output):
    rows = pd.read_csv(root / "fio" / "comparison.csv")
    require({"workload", "tool_mib_s", "fio_mib_s", "mib_delta_pct", "verdict"} <= set(rows),
            "missing fio comparison columns")
    require({"sync-1-seq-4k", "sync-1-rand-4k"} <= set(rows.workload) and not rows.workload.duplicated().any(),
            "fio plot must include both buffered workloads without duplicate cells")
    numeric = rows[["tool_mib_s", "fio_mib_s", "mib_delta_pct"]]
    require(np.isfinite(numeric).all().all() and rows.fio_mib_s.gt(0).all() and rows.tool_mib_s.gt(0).all(),
            "invalid fio comparison numbers")
    delta = (rows.tool_mib_s / rows.fio_mib_s - 1) * 100
    require(np.allclose(delta, rows.mib_delta_pct, atol=0.15, rtol=0), "fio delta arithmetic mismatch")
    require(np.array_equal(rows.verdict.eq("pass"), rows.mib_delta_pct.abs().le(10)), "fio verdict mismatch")
    fig, axes = plt.subplots(1, 2, figsize=(11, 5))
    buffered = rows.workload.str.startswith("sync-")
    for ax, selection, title in zip(axes, [buffered, ~buffered], ["Buffered cached reads", "Direct reads"]):
        subset = rows[selection]
        require(not subset.empty, "empty fio comparison panel")
        x = np.arange(len(subset))
        ax.bar(x - 0.2, subset.tool_mib_s, 0.4, label="iobench")
        ax.bar(x + 0.2, subset.fio_mib_s, 0.4, label="fio")
        for position, (_, row) in zip(x, subset.iterrows()):
            ax.annotate(f"{row.mib_delta_pct:+.1f}%", (position, max(row.tool_mib_s, row.fio_mib_s)),
                        xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9)
        ax.set_xticks(x, subset.workload, rotation=25, ha="right", fontsize=8)
        ax.set(title=title, xlabel="Workload (engine-depth-pattern-block)", ylabel="Throughput (MiB/s)",
               ylim=(0, subset[["tool_mib_s", "fio_mib_s"]].max().max() * 1.2))
        ax.legend()
    fig.suptitle("fio comparison — buffered divergences included")
    save(fig, output, "fio-comparison.png",
         "fio/comparison.csv; separate published three-pair run, not the curve-collection environment.\n"
         "Annotations: (iobench − fio) / fio; fixed ±10% throughput tolerance; panels have different y-scales.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="directory with throughput.csv, depth.csv, distribution CSVs and fio/")
    parser.add_argument("--output", type=Path, default=Path("docs/plots"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    throughput(args.results, args.output)
    depth(args.results, args.output)
    distribution(args.results, args.output)
    fio(args.results, args.output)


if __name__ == "__main__":
    main()
