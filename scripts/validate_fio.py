"""Run identical read workloads through iobench and fio and report the deltas."""

import argparse
import csv
from datetime import datetime, timezone
import io
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

TOLERANCE_PCT = 10.0  # Throughput gate, fixed before the first comparison run.
FILE_SIZE = "1g"
# Comparison cells: (engine, depth, fio ioengine, direct, pattern, fio rw, block).
# Kept small on purpose: both buffered paths, the depth-1 vs 32 queue comparison,
# and one larger random block per path. Add cells freely; time grows linearly.
WORKLOADS = [
    ("sync", 1, "psync", 0, "seq", "read", "4k"),
    ("sync", 1, "psync", 0, "rand", "randread", "4k"),
    ("odirect", 1, "psync", 1, "seq", "read", "4k"),
    ("odirect", 1, "psync", 1, "rand", "randread", "4k"),
    ("odirect", 1, "psync", 1, "rand", "randread", "64k"),
    ("iouring", 32, "io_uring", 1, "rand", "randread", "4k"),
]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path, help="release iobench binary")
parser.add_argument("scratch", type=Path, help="existing directory on the block-backed filesystem under test")
parser.add_argument("--duration", type=int, default=5, help="measured seconds per run (default: 5)")
parser.add_argument("--repeats", type=int, default=3, help="alternating tool/fio pairs per workload (default: 3)")
args = parser.parse_args()
binary = str(args.binary.resolve())
output = Path("results") / f"validate-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}"


def tool(target, engine, depth, pattern, block):
    command = [binary, "--file", str(target), "--file-size", FILE_SIZE, "--engine", engine, "--iodepth", str(depth),
               "--pattern", pattern, "--op", "read", "--block-size", block, "--duration", f"{args.duration}s",
               "--warmup", "1s", "--output", "csv"]
    text = subprocess.check_output(command, text=True)
    header, summary = text.splitlines()[0], text.splitlines()[-1]
    row = next(csv.DictReader(io.StringIO(f"{header}\n{summary}\n")))
    assert row["record_type"] == "summary" and row["submitted"] == row["completed"] == row["operations"]
    return f"{header}\n{summary}\n", dict(mib_s=float(row["mib_per_s"]), mean_us=float(row["mean_us"]), p99_us=float(row["p99_us"]))


def fio(target, ioengine, direct, depth, rw, block):
    command = ["fio", "--name=validate", f"--filename={target}", f"--size={FILE_SIZE}", f"--rw={rw}", f"--bs={block}",
               f"--ioengine={ioengine}", f"--direct={direct}", f"--iodepth={depth}", f"--runtime={args.duration}",
               "--time_based", "--ramp_time=1", "--invalidate=0", "--norandommap", "--lat_percentiles=1",
               "--output-format=json"]
    text = subprocess.check_output(command, text=True)
    job, = json.loads(text)["jobs"]
    assert job["error"] == 0 and job["read"]["io_bytes"] > 0
    latency = job["read"]["lat_ns"]
    return text, dict(mib_s=job["read"]["bw_bytes"] / 2**20, mean_us=latency["mean"] / 1000,
                      p99_us=latency["percentile"]["99.000000"] / 1000)


def warm(target):
    with target.open("rb") as stream:
        while stream.read(1 << 20):
            pass


def delta(ours, reference):
    return (ours - reference) / reference * 100


rows = []
with tempfile.TemporaryDirectory(dir=args.scratch) as directory:
    target = Path(directory) / "validate.data"
    subprocess.check_output([binary, "--file", str(target), "--file-size", FILE_SIZE, "--op", "write",
                             "--block-size", "1m", "--ops", "1024", "--warmup", "0"])
    with target.open("r+b") as stream:
        os.fsync(stream.fileno())
    for engine, depth, ioengine, direct, pattern, rw, block in WORKLOADS:
        name = f"{engine}-{depth}-{pattern}-{block}"
        folder = output / name
        folder.mkdir(parents=True)
        runs = {"tool": [], "fio": []}
        for repeat in range(1, args.repeats + 1):
            order = ["tool", "fio"] if repeat % 2 else ["fio", "tool"]
            for which in order:
                if not direct:
                    warm(target)  # Both tools measure a fully cached file.
                if which == "tool":
                    raw, result = tool(target, engine, depth, pattern, block)
                    (folder / f"tool-{repeat}.csv").write_text(raw)
                else:
                    raw, result = fio(target, ioengine, direct, depth, rw, block)
                    (folder / f"fio-{repeat}.json").write_text(raw)
                runs[which].append(result)
        median = {which: {key: statistics.median(r[key] for r in results) for key in results[0]}
                  for which, results in runs.items()}
        ours, ref = median["tool"], median["fio"]
        mib_delta = delta(ours["mib_s"], ref["mib_s"])
        rows.append(dict(workload=name, tool_mib_s=f"{ours['mib_s']:.1f}", fio_mib_s=f"{ref['mib_s']:.1f}",
                         mib_delta_pct=f"{mib_delta:+.1f}", tool_mean_us=f"{ours['mean_us']:.1f}",
                         fio_mean_us=f"{ref['mean_us']:.1f}", mean_delta_pct=f"{delta(ours['mean_us'], ref['mean_us']):+.1f}",
                         tool_p99_us=f"{ours['p99_us']:.1f}", fio_p99_us=f"{ref['p99_us']:.1f}",
                         verdict="pass" if abs(mib_delta) <= TOLERANCE_PCT else "FAIL"))
        print(" ".join(f"{k}={v}" for k, v in rows[-1].items()), flush=True)

with (output / "comparison.csv").open("w") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
table = ["| " + " | ".join(rows[0]) + " |", "|" + "---|" * len(rows[0])]
table += ["| " + " | ".join(row.values()) + " |" for row in rows]
(output / "comparison.md").write_text("\n".join(table) + "\n")
print(f"\n{output}/comparison.md\n" + "\n".join(table))
failures = [row["workload"] for row in rows if row["verdict"] == "FAIL"]
if failures:
    raise SystemExit(f"throughput delta above {TOLERANCE_PCT}%: {', '.join(failures)}")
