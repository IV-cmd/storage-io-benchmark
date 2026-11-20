#include "common/stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace iobench {

Stats Summarize(std::vector<double> samples_us, std::size_t block_size,
                std::chrono::duration<double> elapsed) {
  if (!std::isfinite(elapsed.count()) || elapsed.count() <= 0) {
    throw std::invalid_argument("elapsed time must be positive and finite");
  }
  if (samples_us.empty()) {
    return {};
  }
  std::ranges::sort(samples_us);
  const auto count = static_cast<double>(samples_us.size());
  const auto percentile = [&](double fraction) {
    const double position = (count - 1) * fraction;
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(lower + 1, samples_us.size() - 1);
    return std::lerp(samples_us[lower], samples_us[upper],
                     position - static_cast<double>(lower));
  };
  const double iops = count / elapsed.count();
  return {samples_us.front(),
          std::accumulate(samples_us.begin(), samples_us.end(), 0.0) / count,
          percentile(0.50),
          percentile(0.95),
          percentile(0.99),
          samples_us.back(),
          iops * static_cast<double>(block_size) / (1024 * 1024),
          iops};
}

}  // namespace iobench
