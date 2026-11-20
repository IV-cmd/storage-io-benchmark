"""Release-mode engine checks; timings here are not performance claims."""

import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
output = Path("results/matrix")
output.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(dir=sys.argv[2]) as directory:
    target = Path(directory) / "workload.data"
    common = [binary, "--file", str(target), "--file-size", "16m", "--ops", "37", "--warmup", "1ms"]
    for engine, depth in [("sync", 1), ("odirect", 1), ("iouring", 1), ("iouring", 32)]:
        for pattern in ["seq", "rand"]:
            for operation in ["read", "write"]:
                for block in [4096, 65536, 1048576]:
                    name = f"{engine}-{depth}-{pattern}-{operation}-{block}"
                    command = common + ["--engine", engine, "--iodepth", str(depth), "--pattern", pattern, "--op", operation, "--block-size", str(block), "--output", "csv"]
                    text = subprocess.check_output(command, text=True)
                    (output / f"{name}.csv").write_text(text)
                    rows = list(csv.DictReader(io.StringIO(text)))
                    assert all(None not in row and None not in row.values() for row in rows)
                    assert all(row["schema_version"] == "2" and row["engine"] == engine and int(row["iodepth"]) == depth for row in rows)
                    samples = [row for row in rows if row["record_type"] == "sample"]
                    summary, = [row for row in rows if row["record_type"] == "summary"]
                    assert len(samples) == int(summary["operations"]) == int(summary["submitted"]) == int(summary["completed"]) == 37
                    assert int(summary["bytes"]) == 37 * block
                    assert int(summary["max_inflight"]) == depth
                    assert all(int(row["offset"]) % block == 0 and int(row["latency_ns"]) >= 0 for row in samples)
                    print(name, "passed")
    assert target.read_bytes() == b"\x5a" * (16 * 1024 * 1024)

    with tempfile.TemporaryDirectory(dir="/dev/shm") as memory_directory:
        cases = [
            (["--engine", "odirect", "--block-size", "1"], ["misaligned direct-I/O"]),
            (["--engine", "sync", "--iodepth", "32"], ["require --iodepth 1"]),
            (["--engine", "odirect", "--file", str(Path(memory_directory) / "reject.data")], ["open O_DIRECT", "tmpfs/OverlayFS"]),
        ]
        with (output / "expected-errors.txt").open("w") as log:
            for extra, messages in cases:
                result = subprocess.run(common + extra, capture_output=True, text=True)
                assert result.returncode == 2 and not result.stdout
                assert any(message in result.stderr for message in messages), result.stderr
                log.write(result.stderr)
                print(result.stderr.strip())
