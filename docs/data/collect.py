"""Reproduce presentation curves on an otherwise idle block-backed Linux host."""

import csv
import gzip
import io
from itertools import product
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

binary, scratch, destination = sys.argv[1:]
binary = str(Path(binary).resolve())
destination = Path(destination)
destination.mkdir(parents=True, exist_ok=True)
size = 256 * 2**20
metadata = dict(commit=os.environ.get("GITHUB_SHA"), run_id=os.environ.get("GITHUB_RUN_ID"),
                file_size=size, repeats=3, throughput_duration_s=2, depth_duration_s=5,
                cache_policy="buffered reads preloaded; direct reads evicted; setup fsync outside timing",
                records=[])


def command(args):
    return subprocess.check_output(list(map(str, args)), text=True)


with tempfile.TemporaryDirectory(dir=scratch) as directory, tempfile.TemporaryDirectory(dir="/dev/shm") as stage:
    target = Path(directory) / "plot.data"
    output = Path(stage)
    common = [binary, "--file", str(target), "--file-size", "256m", "--warmup", "0"]
    metadata["environment"] = {name: command(args) for name, args in {
        "kernel": ["uname", "-a"],
        "filesystem": ["findmnt", "-T", directory, "-o", "SOURCE,FSTYPE,OPTIONS"],
        "devices": ["lsblk", "-o", "NAME,TYPE,SIZE,LOG-SEC,PHY-SEC,ROTA,MODEL"],
    }.items()}
    prepare = common + ["--op", "write", "--block-size", "1m", "--ops", "256"]
    metadata["prepare_command"] = shlex.join(prepare)
    command(prepare)

    def measure(group, engine, pattern, operation, block, depth=1, repeat=1):
        with target.open("rb") as stream:
            os.fsync(stream.fileno())
            os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
            assert int(command(["fincore", "-n", "-b", "-o", "RES", target])) == 0
            if engine == "sync" and operation == "read":
                while stream.read(2**20):
                    pass
        before = int(command(["fincore", "-n", "-b", "-o", "RES", target]))
        if engine == "sync" and operation == "read":
            assert before == size
        cmd = common + ["--engine", engine, "--iodepth", str(depth), "--pattern", pattern,
                        "--op", operation, "--block-size", str(block), "--output", "csv"]
        samples = group == "distribution"
        cmd += ["--ops", "100000"] if samples else ["--duration", "5s" if group == "depth" else "2s"]
        path = output / (f"distribution-{engine}.csv.gz" if samples else f"{group}.csv")
        exists = path.exists()
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True) as process:
            header = process.stdout.readline()
            count, last = 0, ""
            with (gzip.open(path, "wt") if samples else path.open("a")) as stream:
                if samples or not exists:
                    stream.write(header)
                for line in process.stdout:
                    count += line.startswith("2,sample,")
                    if samples:
                        stream.write(line)
                    last = line
                assert process.wait() == 0
                if not samples:
                    stream.write(last)
        summary, = list(csv.DictReader(io.StringIO(header + last)))
        assert summary["record_type"] == "summary"
        assert count == int(summary["operations"]) == int(summary["submitted"]) == int(summary["completed"])
        assert int(summary["bytes"]) == count * block
        metadata["records"].append(dict(group=group, repeat=repeat, command=shlex.join(cmd),
                                        resident_before=before,
                                        resident_after=int(command(["fincore", "-n", "-b", "-o", "RES", target]))))
        print(group, repeat, engine, pattern, operation, block, depth, summary["mib_per_s"], flush=True)

    matrix = list(product(["sync", "odirect", "iouring"], ["seq", "rand"],
                          ["read", "write"], [4096, 65536, 1048576]))
    for repeat in range(1, 4):
        for case in matrix if repeat % 2 else reversed(matrix):
            measure("throughput", *case, repeat=repeat)
    depths = [1, 4, 8, 16, 32, 64, 128]
    for repeat in range(1, 4):
        for depth in depths if repeat % 2 else reversed(depths):
            measure("depth", "iouring", "rand", "read", 4096, depth, repeat)
    for engine in ["sync", "odirect", "iouring"]:
        measure("distribution", engine, "rand", "read", 4096)
    (output / "sources.json").write_text(json.dumps(metadata, indent=2) + "\n")
    shutil.copy2(__file__, output / "collect.py")
    shutil.copytree(output, destination, dirs_exist_ok=True)
