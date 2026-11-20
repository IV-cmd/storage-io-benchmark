#include <liburing.h>

#include <cerrno>
#include <exception>
#include <limits>
#include <stdexcept>
#include <system_error>

#include "bench/io.h"

namespace iobench {
namespace {

class IoUringEngine final : public Io {
 public:
  explicit IoUringEngine(const Config& config)
      : Io(config, true, config.iodepth) {
    if (config.block_size > std::numeric_limits<unsigned>::max()) {
      throw std::invalid_argument(
          "io_uring block size exceeds its length field");
    }
    const int error = io_uring_queue_init(config.iodepth, &ring_, 0);
    if (error < 0) {
      throw std::system_error(-error, std::generic_category(),
                              "io_uring setup");
    }
  }

  ~IoUringEngine() override {
    // Submitted requests must finish before the base class frees their buffers.
    try {
      while (completed < submitted) {
        Reap();
      }
    } catch (...) {
      std::terminate();
    }
    io_uring_queue_exit(&ring_);
  }

  void Submit(unsigned slot, std::uint64_t offset) override {
    CheckAlignment(Buffer(slot), offset, config_.block_size, alignment_);
    auto* sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
      throw std::runtime_error("io_uring submission queue full");
    }
    const auto length = static_cast<unsigned>(config_.block_size);
    if (config_.operation == Operation::Write) {
      io_uring_prep_write(sqe, fd_, Buffer(slot), length, offset);
    } else {
      io_uring_prep_read(sqe, fd_, Buffer(slot), length, offset);
    }
    io_uring_sqe_set_data64(sqe, slot);
    ++pending_;
  }

  void Flush() override {
    while (pending_ != 0) {
      const int count = io_uring_submit(&ring_);
      if (count == -EINTR) {
        continue;
      }
      if (count < 0) {
        throw std::system_error(-count, std::generic_category(),
                                "io_uring submit");
      }
      if (count == 0 || static_cast<unsigned>(count) > pending_) {
        throw std::runtime_error("io_uring made invalid submission progress");
      }
      pending_ -= static_cast<unsigned>(count);
      submitted += static_cast<unsigned>(count);
    }
  }

  unsigned Complete() override {
    const auto completion = Reap();
    if (completion.res < 0) {
      throw std::system_error(-completion.res, std::generic_category(),
                              "io_uring completion");
    }
    if (static_cast<std::uint64_t>(completion.res) != config_.block_size) {
      throw std::runtime_error("short io_uring transfer");
    }
    if (completion.user_data >= Depth()) {
      throw std::runtime_error("invalid io_uring completion slot");
    }
    return static_cast<unsigned>(completion.user_data);
  }

 private:
  io_uring_cqe Reap() {
    io_uring_cqe* cqe = nullptr;
    int error = 0;
    do {
      error = io_uring_wait_cqe(&ring_, &cqe);
    } while (error == -EINTR);
    if (error < 0) {
      throw std::system_error(-error, std::generic_category(), "io_uring wait");
    }
    const auto completion = *cqe;
    io_uring_cqe_seen(&ring_, cqe);
    ++completed;
    return completion;
  }

  io_uring ring_{};
  unsigned pending_ = 0;
};

}  // namespace

std::unique_ptr<Io> MakeIoUring(const Config& config) {
  return std::make_unique<IoUringEngine>(config);
}

}  // namespace iobench
