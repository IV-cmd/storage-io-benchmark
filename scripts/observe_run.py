"""Capture iostat, blktrace/btt, and perf stat alongside iobench runs."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

WORKLOADS = {
    "sync-1-seq-64k": ["--engine", "sync", "--pattern", "seq", "--block-size", "64k"],
    "odirect-1-rand-4k": ["--engine", "odirect", "--pattern", "rand", "--block-size", "4k"],
    "iouring-32-rand-4k": ["--engine", "iouring", "--iodepth", "32", "--pattern", "rand", "--block-size", "4k"],
}
SUDO = [] if os.geteuid() == 0 else ["sudo", "-n"]
PERF_EVENTS = "task-clock,context-switches,cpu-migrations,page-faults"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path, help="release iobench binary")
parser.add_argument("scratch", type=Path, help="existing directory on the block-backed filesystem under test")
parser.add_argument("--duration", type=int, default=2, help="measured seconds per workload (default: 2)")
args = parser.parse_args()
binary = str(args.binary.resolve())
output = Path("results") / f"observe-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}"


def disk_of(path):
    number = path.stat().st_dev
    node = Path(f"/sys/dev/block/{os.major(number)}:{os.minor(number)}").resolve(strict=True)
    return node.parent.name if (node / "partition").exists() else node.name


def stop(process):
    process.send_signal(signal.SIGINT)
    try:
        process.wait(5)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


@contextmanager
def iostat(path, disk):
    if not shutil.which("iostat"):
        path.write_text("unavailable: iostat (sysstat) is not installed\n")
        yield
        return
    with path.open("w") as stream:
        process = subprocess.Popen(["iostat", "-dxy", "-t", disk, "1"], stdout=stream, env={**os.environ, "LC_ALL": "C"})
        try:
            yield
            time.sleep(1.1)  # Let the final one-second interval print.
        finally:
            stop(process)


@contextmanager
def blktrace(folder, disk):
    text = folder / "blktrace.txt"
    missing = [name for name in ("blktrace", "blkparse", "btt") if not shutil.which(name)]
    if missing:
        text.write_text("unavailable: missing " + ", ".join(missing) + "\n")
        yield
        return
    # Traces go to tmpfs so writing them does not add I/O to the traced disk.
    with tempfile.TemporaryDirectory(dir="/dev/shm") as trace:
        process = subprocess.Popen(SUDO + ["blktrace", "-d", f"/dev/{disk}", "-D", trace, "-o", "trace", "-w", str(args.duration + 4)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        time.sleep(1)  # blktrace gives no readiness signal; an early exit means setup failed.
        if process.poll() is not None:
            text.write_text(f"unavailable: blktrace needs root/CAP_SYS_ADMIN and debugfs: {process.stderr.read()}")
            yield
            return
        try:
            yield
        finally:
            stop(process)
        with text.open("w") as stream:
            subprocess.run(SUDO + ["blkparse", "-i", "trace", "-D", trace, "-d", f"{trace}/trace.bin"], stdout=stream, check=True)
        # btt also writes <dev>_iops_fp.dat dumps; keep them inside the temp dir.
        with (folder / "btt.txt").open("w") as stream:
            subprocess.run(["btt", "-i", f"{trace}/trace.bin"], stdout=stream, cwd=trace, check=True)


def evict(target):
    with target.open("r+b") as stream:
        os.fsync(stream.fileno())
        os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)


def environment(scratch, disk, perf):
    commands = [["uname", "-a"], ["findmnt", "-T", str(scratch), "-o", "SOURCE,FSTYPE,OPTIONS"],
                ["lsblk", "-o", "NAME,TYPE,SIZE,LOG-SEC,PHY-SEC,ROTA,MODEL"]]
    lines = [f"disk={disk}", f"perf_stat={'available' if perf else 'unavailable'}", f"perf_events={PERF_EVENTS}",
             "profile_scope=whole process: setup, warmup, measurement, statistics and CSV formatting",
             "block_scope=whole device, including unrelated processes",
             "output_capture=tmpfs; copied to results only after all monitors stop"]
    scheduler = Path(f"/sys/block/{disk}/queue/scheduler")
    lines.append(f"scheduler={scheduler.read_text().strip() if scheduler.exists() else 'not exposed'}")
    lines += [subprocess.run(command, capture_output=True, text=True).stdout for command in commands]
    return "\n".join(lines)


probe = subprocess.run(["perf", "stat", "-e", PERF_EVENTS, "--", "true"], capture_output=True, text=True,
                       env={**os.environ, "LC_ALL": "C"}) if shutil.which("perf") else None
perf_ok = probe is not None and probe.returncode == 0 and not any(
    marker in probe.stderr for marker in ("<not supported>", "<not counted>"))
perf_reason = probe.stderr.strip() if probe is not None else "perf is not installed"
with tempfile.TemporaryDirectory(dir=args.scratch) as directory:
    target = Path(directory) / "observe.data"
    common = [binary, "--file", str(target), "--file-size", "1g", "--op", "read"]
    subprocess.check_output([binary, "--file", str(target), "--file-size", "1g", "--op", "write",
                             "--block-size", "1m", "--ops", "1024", "--warmup", "0"])
    disk = disk_of(target)
    output.mkdir(parents=True)
    (output / "environment.txt").write_text(environment(args.scratch, disk, perf_ok))
    for name, workload in WORKLOADS.items():
        folder = output / name
        folder.mkdir()
        command = common + workload + ["--duration", f"{args.duration}s", "--output", "csv"]
        evict(target)  # Request file-page eviction; warmup can populate them again.
        with tempfile.TemporaryDirectory(dir="/dev/shm") as directory:
            capture = Path(directory)
            prefix = ["perf", "stat", "-e", PERF_EVENTS, "-o", str(capture / "perf.txt"), "--"] if perf_ok else []
            (capture / "command.txt").write_text(" ".join(prefix + command) + "\n")
            if not perf_ok:
                (capture / "perf.txt").write_text(f"unavailable: {perf_reason}\n")
            with (capture / "tool.csv").open("w") as stream, iostat(capture / "iostat.txt", disk), blktrace(capture, disk):
                subprocess.run(prefix + command, stdout=stream, check=True)
            shutil.copytree(capture, folder, dirs_exist_ok=True)
        print(f"{folder}: " + ", ".join(sorted(p.name for p in folder.iterdir())), flush=True)
