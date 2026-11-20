#include "common/stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace iobench {
namespace {

double SortedPercentile(std::span<const double> sorted, double percentile) {
  if (sorted.empty()) {
    return 0;
  }
  const double position = percentile / 100 * (sorted.size() - 1);
  const auto lower = static_cast<std::size_t>(position);
  const auto upper = std::min(lower + 1, sorted.size() - 1);
  return std::lerp(sorted[lower], sorted[upper], position - lower);
}

}  // namespace

double Percentile(std::span<const double> samples_us, double percentile) {
  if (!std::isfinite(percentile) || percentile < 0 || percentile > 100) {
    throw std::invalid_argument("percentile must be between 0 and 100");
  }
  std::vector<double> sorted(samples_us.begin(), samples_us.end());
  std::ranges::sort(sorted);
  return SortedPercentile(sorted, percentile);
}

LatencyStats SummarizeLatency(std::span<const double> samples_us) {
  if (samples_us.empty()) {
    return {};
  }
  std::vector<double> sorted(samples_us.begin(), samples_us.end());
  std::ranges::sort(sorted);
  return {sorted.front(),
          std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size(),
          SortedPercentile(sorted, 50),
          SortedPercentile(sorted, 95),
          SortedPercentile(sorted, 99),
          sorted.back()};
}

RunStats SummarizeRun(std::span<const double> samples_us,
                      std::size_t block_size, double elapsed_seconds) {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0) {
    throw std::invalid_argument("elapsed time must be positive and finite");
  }
  const double iops = samples_us.size() / elapsed_seconds;
  return {SummarizeLatency(samples_us), iops * block_size / (1024 * 1024),
          iops};
}

}  // namespace iobench
