#include "bench/io.h"

#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace iobench {
namespace {

class SyncEngine : public Io {
 public:
  explicit SyncEngine(const Config& config, bool direct = false)
      : Io(config, direct, 1) {}

  void Submit(unsigned slot, std::uint64_t offset) override {
    Transfer(fd_, std::span(Buffer(slot), config_.block_size), offset,
             config_.operation == Operation::Write, alignment_);
    ++submitted;
  }

  unsigned Complete() override {
    ++completed;
    return 0;
  }
};

class ODirectEngine final : public SyncEngine {
 public:
  explicit ODirectEngine(const Config& config) : SyncEngine(config, true) {}
};

}  // namespace

void Transfer(int fd, std::span<char> buffer, std::uint64_t offset, bool write,
              Alignment alignment) {
  std::size_t done = 0;
  while (done < buffer.size()) {
    CheckAlignment(buffer.data() + done, offset + done, buffer.size() - done,
                   alignment);
    const auto position = static_cast<off_t>(offset + done);
    const auto count =
        write ? pwrite(fd, buffer.data() + done, buffer.size() - done, position)
              : pread(fd, buffer.data() + done, buffer.size() - done, position);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::generic_category(), "pread/pwrite");
    }
    if (count == 0) {
      throw std::runtime_error("short transfer: EOF or no write progress");
    }
    done += static_cast<std::size_t>(count);
  }
}

std::unique_ptr<Io> MakeIo(const Config& config) {
  if (config.iodepth != 1) {
    throw std::invalid_argument("sync and odirect require --iodepth 1");
  }
  std::unique_ptr<Io> io;
  if (config.engine == "sync") {
    io = std::make_unique<SyncEngine>(config);
  } else if (config.engine == "odirect") {
    io = std::make_unique<ODirectEngine>(config);
  } else {
    throw std::invalid_argument("unknown engine");
  }
  io->Prepare();
  return io;
}

}  // namespace iobench
