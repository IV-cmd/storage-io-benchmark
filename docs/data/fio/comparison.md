| workload | tool_mib_s | fio_mib_s | mib_delta_pct | tool_mean_us | fio_mean_us | mean_delta_pct | tool_p99_us | fio_p99_us | verdict |
|---|---|---|---|---|---|---|---|---|---|
| sync-1-seq-4k | 4245.3 | 3442.0 | +23.3 | 0.8 | 1.0 | -13.1 | 1.2 | 1.3 | FAIL |
| sync-1-rand-4k | 3733.4 | 3097.6 | +20.5 | 1.0 | 1.1 | -11.0 | 1.3 | 1.4 | FAIL |
| odirect-1-seq-4k | 30.1 | 30.6 | -1.6 | 129.8 | 127.5 | +1.8 | 239.4 | 242.7 | pass |
| odirect-1-rand-4k | 30.0 | 30.4 | -1.4 | 130.1 | 127.7 | +1.8 | 241.1 | 222.2 | pass |
| odirect-1-rand-64k | 308.4 | 311.1 | -0.9 | 202.5 | 200.5 | +1.0 | 378.4 | 342.0 | pass |
| iouring-32-rand-4k | 76.7 | 76.5 | +0.3 | 1629.2 | 1647.9 | -1.1 | 41691.6 | 41680.9 | pass |
