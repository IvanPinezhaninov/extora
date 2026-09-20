#============================================================================
#
# Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
#
# This file is part of the extora which can be found at
# https://github.com/IvanPinezhaninov/extora/.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
# THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

set(extora_benchmark_min_version 1.9.5)
set(extora_bundled_benchmark_version 1.9.5)

if(TARGET benchmark::benchmark)
  message(STATUS "Using Google Benchmark target provided by the parent project")
elseif(EXTORA_USE_BUNDLED_BENCHMARK)
  include(FetchContent)

  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable Google Benchmark gtest tests" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable Google Benchmark installation" FORCE)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable Google Benchmark tests" FORCE)
  FetchContent_Declare(
    extora_benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v${extora_bundled_benchmark_version}.tar.gz
    URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(extora_benchmark)
  message(STATUS "Using bundled Google Benchmark ${extora_bundled_benchmark_version}")
else()
  find_package(benchmark ${extora_benchmark_min_version} CONFIG REQUIRED)
  extora_promote_imported_target(benchmark::benchmark)
  message(STATUS "Using system Google Benchmark")
endif()

if(NOT TARGET benchmark::benchmark)
  message(FATAL_ERROR "The selected Google Benchmark dependency must provide benchmark::benchmark")
endif()
