BUILD_DIR ?= build
CLANG_FORMAT ?= clang-format-18
CLANG_TIDY ?= clang-tidy-18
SOURCES := $(shell find src tests -type f \( -name '*.cc' -o -name '*.h' \))

.PHONY: all build test format format-check tidy test-asan
all: build

build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --parallel 2

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

format:
	$(CLANG_FORMAT) -i $(SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

tidy: build
	$(CLANG_TIDY) -p $(BUILD_DIR) $(filter %.cc,$(SOURCES))

test-asan:
	$(MAKE) test BUILD_DIR=build-asan \
	  CMAKE_FLAGS="-DIOBENCH_SANITIZERS=ON -DCMAKE_CXX_COMPILER=clang++-18"
