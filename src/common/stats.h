#pragma once

#include <cstddef>
#include <span>

namespace iobench {

struct LatencyStats {
  double min_us;
  double mean_us;
  double p50_us;
  double p95_us;
  double p99_us;
  double max_us;
};

struct RunStats {
  LatencyStats latency;
  double mib_per_second;
  double iops;
};

double Percentile(std::span<const double> samples_us, double percentile);
LatencyStats SummarizeLatency(std::span<const double> samples_us);
RunStats SummarizeRun(std::span<const double> samples_us,
                      std::size_t block_size, double elapsed_seconds);

}  // namespace iobench
