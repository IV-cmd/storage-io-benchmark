"""Collect exploratory cache and queue-depth evidence on a disposable Linux VM."""

import csv
from datetime import datetime, timezone
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
output = Path("results/observations")
output.mkdir(parents=True, exist_ok=True)


def save(name, rows):
    with (output / name).open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


with tempfile.TemporaryDirectory(dir=sys.argv[2]) as directory:
    target = Path(directory) / "read.data"
    common = [binary, "--file", str(target), "--file-size", "256m", "--output", "csv"]

    def bench(name, arguments):
        text = subprocess.check_output(common + arguments, text=True)
        summary = next(row for row in csv.DictReader(io.StringIO(text)) if row["record_type"] == "summary")
        save(f"{name}-summary.csv", [summary])
        return summary

    bench("prepare", ["--engine", "sync", "--op", "write", "--block-size", "1m", "--ops", "256", "--warmup", "0"])
    device_number = target.stat().st_dev
    device = Path(f"/sys/dev/block/{os.major(device_number)}:{os.minor(device_number)}").resolve(strict=True)
    queue = device / "queue" if (device / "queue").exists() else device.parent / "queue"
    with (output / "device.txt").open("w") as stream:
        stream.write(f"device={device}\npage_size={os.sysconf('SC_PAGESIZE')}\n")
        for name in ["logical_block_size", "physical_block_size", "scheduler", "rotational"]:
            attribute = queue / name
            value = attribute.read_text().strip() if attribute.exists() else "not exposed at this device layer"
            stream.write(f"{name}={value}\n")

    def evict():
        with target.open("r+b") as stream:
            os.fsync(stream.fileno())
            os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)

    def pages():
        return int(subprocess.check_output(["fincore", "--noheadings", "--output", "PAGES", str(target)], text=True))

    def sectors():
        return int((device / "stat").read_text().split()[2])

    with (output / "iostat.txt").open("w") as stream:
        monitor = subprocess.Popen(["iostat", "-dx", "-t", "-y", "1"], stdout=stream)
        try:
            cache_rows = []
            for name, engine, cold in [("buffered-cold", "sync", True), ("buffered-warm", "sync", False), ("direct-cold", "odirect", True), ("direct-repeat", "odirect", False)]:
                if cold:
                    evict()
                before, reads_before = pages(), sectors()
                started = datetime.now(timezone.utc).isoformat()
                summary = bench(name, ["--engine", engine, "--op", "read", "--pattern", "seq", "--block-size", "64k", "--duration", "2s", "--warmup", "0"])
                ended = datetime.now(timezone.utc).isoformat()
                after, reads_after = pages(), sectors()
                # Kernel disk-stat sectors are always 512-byte accounting units.
                cache_rows.append(dict(name=name, engine=engine, pages_before=before, pages_after=after, device_read_mib=(reads_after - reads_before) * 512 / 1048576, mib_per_s=summary["mib_per_s"], started_utc=started, ended_utc=ended))
            save("cache.csv", cache_rows)
            assert cache_rows[0]["pages_after"] > cache_rows[0]["pages_before"]
            assert all(row["pages_before"] == row["pages_after"] == 0 for row in cache_rows[2:])

            depth_rows = []
            for repeat, depths in enumerate([(1, 32), (32, 1), (1, 32)], start=1):
                for depth in depths:
                    summary = bench(f"depth-{repeat}-{depth}", ["--engine", "iouring", "--iodepth", str(depth), "--op", "read", "--pattern", "rand", "--block-size", "4k", "--duration", "2s", "--warmup", "1s"])
                    depth_rows.append(dict(repeat=repeat, iodepth=depth, operations=summary["operations"], submitted=summary["submitted"], completed=summary["completed"], max_inflight=summary["max_inflight"], mib_per_s=summary["mib_per_s"], iops=summary["iops"]))
            save("depth.csv", depth_rows)
            for path in ["cache.csv", "depth.csv"]:
                print(path + "\n" + (output / path).read_text())
            time.sleep(1.1)  # Let iostat emit its last interval, not a timing delay between runs.
        finally:
            monitor.send_signal(signal.SIGINT)
            monitor.wait()
