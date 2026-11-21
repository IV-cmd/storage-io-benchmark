BUILD_DIR ?= build
RELEASE_DIR ?= build-release
CLANG_FORMAT ?= clang-format-18
CLANG_TIDY ?= clang-tidy-18
SOURCES := $(shell find src tests -type f \( -name '*.cc' -o -name '*.h' \))

.PHONY: all build release test format format-check tidy test-asan validate observe
all: build

build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --parallel 2

release:
	cmake -S . -B $(RELEASE_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF $(CMAKE_FLAGS)
	cmake --build $(RELEASE_DIR) --parallel 2

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

validate: release
	@test -n "$(SCRATCH)" || { echo "usage: make validate SCRATCH=/existing/dir/on/tested/filesystem"; exit 2; }
	python3 scripts/validate_fio.py $(RELEASE_DIR)/iobench $(SCRATCH)

observe: release
	@test -n "$(SCRATCH)" || { echo "usage: make observe SCRATCH=/existing/dir/on/tested/filesystem"; exit 2; }
	python3 scripts/observe_run.py $(RELEASE_DIR)/iobench $(SCRATCH)
