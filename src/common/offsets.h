#pragma once

#include <cstdint>
#include <random>
#include <stdexcept>

namespace iobench {

enum class Pattern { Sequential, Random };

class Offsets {
 public:
  Offsets(std::uint64_t file_size, std::uint64_t block_size, Pattern pattern)
      : block_size_(block_size), pattern_(pattern) {
    if (block_size == 0 || file_size < block_size || file_size % block_size != 0) {
      throw std::invalid_argument("file size must be a positive block multiple");
    }
    blocks_ = file_size / block_size;
    random_block_ = std::uniform_int_distribution<std::uint64_t>(0, blocks_ - 1);
  }

  std::uint64_t Next() {
    const auto block = pattern_ == Pattern::Random ? random_block_(random_)
                                                 : next_;
    next_ = (next_ + 1) % blocks_;
    return block * block_size_;
  }

 private:
  std::uint64_t block_size_;
  Pattern pattern_;
  std::uint64_t blocks_ = 0;
  std::uint64_t next_ = 0;
  std::mt19937_64 random_{42};
  std::uniform_int_distribution<std::uint64_t> random_block_;
};

}  // namespace iobench
