cmake_minimum_required(VERSION 3.20)
project(iobench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IOBENCH_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(IOBENCH_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(iobench_core src/common/stats.cc)
target_include_directories(iobench_core PUBLIC src)
target_compile_options(iobench_core PRIVATE -Wall -Wextra)

add_executable(iobench src/main.cc)
target_link_libraries(iobench PRIVATE iobench_core)
target_compile_options(iobench PRIVATE -Wall -Wextra)

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)

  add_executable(iobench_tests tests/core_test.cc)
  target_include_directories(iobench_tests PRIVATE src)
  target_compile_options(iobench_tests PRIVATE -Wall -Wextra)
  target_link_libraries(iobench_tests PRIVATE iobench_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(iobench_tests)
endif()
