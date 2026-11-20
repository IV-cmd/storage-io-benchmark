#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace iobench {

struct Stats {
  double min_us;
  double mean_us;
  double p50_us;
  double p95_us;
  double p99_us;
  double max_us;
  double mib_per_second;
  double iops;
};

Stats Summarize(std::vector<double> samples_us, std::size_t block_size,
                std::chrono::duration<double> elapsed);

}  // namespace iobench
