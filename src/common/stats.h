#pragma once

#include <span>

namespace iobench {

struct LatencyStats {
  double p50_us;
  double p95_us;
  double p99_us;
};

double Percentile(std::span<const double> samples_us, double percentile);
LatencyStats SummarizeLatency(std::span<const double> samples_us);

}  // namespace iobench
