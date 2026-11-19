#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
  std::string_view engine = "pread";
  std::string_view block_size = "4k";
  std::string_view queue_depth = "1";
  std::string_view file = "iobench.data";
  std::string_view runtime = "10";

  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--help") {
      std::cout << R"(Usage: iobench [options]
  --engine NAME
  --block-size SIZE
  --queue-depth N
  --file PATH
  --runtime SECONDS
Skeleton only: prints options; performs no storage I/O.
)";
      return 0;
    }
    if (i + 1 == argc) {
      std::cerr << "Missing value for " << flag << '\n';
      return 2;
    }
    const std::string_view value = argv[++i];
    if (flag == "--engine") {
      engine = value;
    } else if (flag == "--block-size") {
      block_size = value;
    } else if (flag == "--queue-depth") {
      queue_depth = value;
    } else if (flag == "--file") {
      file = value;
    } else if (flag == "--runtime") {
      runtime = value;
    } else {
      std::cerr << "Unknown option: " << flag << '\n';
      return 2;
    }
  }

  std::cout << "Skeleton only: no file or device I/O is performed.\n"
            << "engine=" << engine << '\n'
            << "block_size=" << block_size << '\n'
            << "queue_depth=" << queue_depth << '\n'
            << "file=" << file << '\n'
            << "runtime_seconds=" << runtime << '\n';
}
