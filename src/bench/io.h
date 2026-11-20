#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "bench/engine.h"

namespace iobench {

struct Alignment {
  std::uint32_t memory = 1;
  std::uint32_t offset = 1;
  std::uint32_t logical_block = 0;
};

Alignment DirectAlignment(int fd);
void CheckAlignment(const void* buffer, std::uint64_t offset,
                    std::size_t length, Alignment alignment);
void Transfer(int fd, std::span<char> buffer, std::uint64_t offset, bool write,
              Alignment alignment = {});

class Io {
 public:
  Io(const Config& config, bool direct, unsigned depth);
  virtual ~Io();
  Io(const Io&) = delete;
  Io& operator=(const Io&) = delete;

  void Prepare();
  virtual void Submit(unsigned slot, std::uint64_t offset) = 0;
  virtual void Flush() {}
  virtual unsigned Complete() = 0;
  [[nodiscard]] unsigned Depth() const { return depth_; }

  std::uint64_t submitted = 0;
  std::uint64_t completed = 0;

 protected:
  [[nodiscard]] char* Buffer(unsigned slot) const {
    return static_cast<char*>(memory_) + slot * stride_;
  }
  const Config& config_;
  int fd_;
  Alignment alignment_;

 private:
  bool direct_;
  unsigned depth_;
  void* memory_ = nullptr;
  std::size_t stride_ = 0;
};

std::unique_ptr<Io> MakeIo(const Config& config);
std::unique_ptr<Io> MakeIoUring(const Config& config);

}  // namespace iobench
