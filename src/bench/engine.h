#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/offsets.h"

namespace iobench {

enum class Operation : std::uint8_t { Read, Write };

struct Config {
  std::string file;
  Pattern pattern = Pattern::Sequential;
  Operation operation = Operation::Read;
  std::uint64_t block_size = 4096;
  std::uint64_t file_size = 1024ULL * 1024 * 1024;
  std::chrono::nanoseconds duration = std::chrono::seconds(5);
  std::chrono::nanoseconds warmup = std::chrono::seconds(1);
  std::uint64_t operations = 0;
};

struct Sample {
  std::uint64_t offset;
  std::int64_t latency_ns;
};

struct Result {
  std::vector<Sample> samples;
  std::int64_t elapsed_ns = 0;
};

class Engine {
 public:
  explicit Engine(Config config) : config_(std::move(config)) {}
  [[nodiscard]] Result Run() const;

 private:
  Config config_;
};

}  // namespace iobench
