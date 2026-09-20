/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include "BenchmarkSupport.h"

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
#include <array>
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif // defined(__linux__)

#include <benchmark/benchmark.h>

#include "extora/extora.h"

namespace extoraBenchmark {

namespace {

constexpr std::size_t rangeObjectSize = 64 * 1024 * 1024;
constexpr std::size_t rangeReadSize = 4 * 1024;
constexpr std::uint64_t rangeStride = 1024 * 1024 + rangeReadSize;

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
struct ExtentSizeCase {
  std::uint64_t bytes;
  const char* name;
};

constexpr std::array<ExtentSizeCase, 9> extentSizeCases{{
    {256 * 1024, "256KiB"},
    {512 * 1024, "512KiB"},
    {1024 * 1024, "1MiB"},
    {2 * 1024 * 1024, "2MiB"},
    {4 * 1024 * 1024, "4MiB"},
    {8 * 1024 * 1024, "8MiB"},
    {16 * 1024 * 1024, "16MiB"},
    {32 * 1024 * 1024, "32MiB"},
    {64 * 1024 * 1024, "64MiB"},
}};
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

void benchPlainFileRead(benchmark::State& state, std::int64_t objectSizeValue, const char* name, const char* sizeName)
{
  const std::size_t objectSize = static_cast<std::size_t>(objectSizeValue);
  const std::vector<std::byte> source = makeBuffer(objectSize);
  const std::filesystem::path root = makeBenchRoot(name, sizeName);
  const std::filesystem::path path = root / "object.bin";
  writePlainFile(path, source, true);

  std::vector<std::byte> buffer(objectSize);

  for ([[maybe_unused]] auto _ : state) {
    readPlainFile(path, buffer);
    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));

  std::filesystem::remove_all(root);
}

void benchExtoraGetObject(benchmark::State& state, std::int64_t objectSizeValue, const char* name, const char* sizeName,
                          bool cacheEnabled, bool concurrencyLimited,
                          std::uint64_t maxExtentSize = extora::default_max_extent_size, bool verifyChecksum = false)
{
  const std::size_t objectSize = static_cast<std::size_t>(objectSizeValue);
  const std::vector<std::byte> source = makeBuffer(objectSize);
  const std::filesystem::path root = makeBenchRoot(name, sizeName);

  extora::storage_error error;
  extora::object_store_options options = makeObjectStoreOptions(root, extora::storage_durability::balanced);
  options.max_extent_size = maxExtentSize;
  if (!cacheEnabled) options.object_lookup_cache_capacity = 0;
  if (concurrencyLimited) options.max_concurrent_reads = 64;

  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);

  if (failed(error) || !store) std::abort();

  extora::bucket_name bucket;
  bucket.value = "bench";

  error = store->create_bucket(bucket);
  if (failed(error)) std::abort();

  extora::object_key key;
  key.value = "object";

  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = static_cast<std::uint64_t>(source.size());

  MemoryObjectReader reader{source};
  extora::put_object_result putResult;
  error = store->put_object(bucket, key, reader, metadata, putResult, putOptions);
  if (failed(error)) std::abort();

  std::vector<std::byte> buffer(objectSize);

  for ([[maybe_unused]] auto _ : state) {
    extora::open_object_options getOptions;
    getOptions.verify_integrity = verifyChecksum;
    MemoryObjectWriter writer{buffer};
    extora::open_object_result result;
    error = readObject(*store, bucket, key, writer, getOptions, result);
    if (failed(error)) std::abort();

    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));

  store.reset();
  std::filesystem::remove_all(root);
}

void plainFileRead(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchPlainFileRead(state, objectSize, "plain_read", sizeName);
}

void cachedObjectRead(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraGetObject(state, objectSize, "extora_get", sizeName, true, false);
}

void uncachedObjectRead(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraGetObject(state, objectSize, "extora_get_uncached", sizeName, false, false);
}

void limitedObjectRead(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraGetObject(state, objectSize, "extora_get_limited", sizeName, true, true);
}

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
void verifiedCachedObjectRead(benchmark::State& state)
{
  benchExtoraGetObject(state, size1GiB, "extora_get_verified", "1GiB", true, false, extora::default_max_extent_size,
                       true);
}

#if defined(__linux__)
bool discardFileCache(const std::filesystem::path& path)
{
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;

  const int adviceError = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
  const int closeError = ::close(fd);
  return adviceError == 0 && closeError == 0;
}

bool discardSegmentCache(const std::filesystem::path& root)
{
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{root / "segments"}) {
    if (!entry.is_regular_file()) continue;
    if (!discardFileCache(entry.path())) return false;
  }
  return true;
}

void evictedPlainFileRead(benchmark::State& state)
{
  constexpr std::size_t objectSize = static_cast<std::size_t>(size1GiB);
  const std::filesystem::path root = makeBenchRoot("plain_read_evicted", "1GiB");
  const std::filesystem::path path = root / "object.bin";
  {
    const std::vector<std::byte> source = makeBuffer(objectSize);
    writePlainFile(path, source, true);
  }

  std::vector<std::byte> buffer(objectSize);
  for ([[maybe_unused]] auto _ : state) {
    state.PauseTiming();
    const bool discarded = discardFileCache(path);
    state.ResumeTiming();
    if (!discarded) {
      state.SkipWithError("failed to evict the plain file from the page cache");
      break;
    }

    readPlainFile(path, buffer);
    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));
  std::filesystem::remove_all(root);
}

void evictedObjectRead(benchmark::State& state)
{
  constexpr std::size_t objectSize = static_cast<std::size_t>(size1GiB);
  const std::filesystem::path root = makeBenchRoot("extora_get_evicted", "1GiB");

  extora::storage_error error;
  const extora::object_store_options options = makeObjectStoreOptions(root, extora::storage_durability::balanced);
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::abort();

  const extora::bucket_name bucket{"bench"};
  error = store->create_bucket(bucket);
  if (failed(error)) std::abort();

  const extora::object_key key{"object"};
  {
    const std::vector<std::byte> source = makeBuffer(objectSize);
    extora::object_metadata metadata;
    extora::put_object_options putOptions;
    putOptions.expected_content_length = static_cast<std::uint64_t>(source.size());
    MemoryObjectReader reader{source};
    extora::put_object_result putResult;
    error = store->put_object(bucket, key, reader, metadata, putResult, putOptions);
    if (failed(error)) std::abort();
  }

  std::vector<std::byte> buffer(objectSize);
  for ([[maybe_unused]] auto _ : state) {
    state.PauseTiming();
    const bool discarded = discardSegmentCache(root);
    state.ResumeTiming();
    if (!discarded) {
      state.SkipWithError("failed to evict segment files from the page cache");
      break;
    }

    MemoryObjectWriter writer{buffer};
    extora::open_object_result result;
    error = readObject(*store, bucket, key, writer, extora::open_object_options{}, result);
    if (failed(error)) {
      state.SkipWithError(error.message.c_str());
      break;
    }
    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));

  store.reset();
  std::filesystem::remove_all(root);
}
#endif // defined(__linux__)

void benchMaxExtentSize(benchmark::State& state, std::uint64_t maxExtentSize, const char* extentName)
{
  constexpr std::int64_t objectSize = 64 * 1024 * 1024;
  const std::string name = std::string{"read_extent_"} + extentName;
  benchExtoraGetObject(state, objectSize, name.c_str(), "64MiB", true, false, maxExtentSize);
}

void registerMaxExtentSizeBenchmarks()
{
  for (const ExtentSizeCase& extentSize : extentSizeCases) {
    const std::string benchmarkName = "Read/Object/Cached/Extent" + std::string{extentSize.name} + "/64MiB";
    configureObjectBenchmark(benchmark::RegisterBenchmark(benchmarkName.c_str(), [extentSize](benchmark::State& state) {
      benchMaxExtentSize(state, extentSize.bytes, extentSize.name);
    }));
  }
}
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

void objectRangeRead(benchmark::State& state)
{
  const std::vector<std::byte> source = makeBuffer(rangeObjectSize);
  const std::filesystem::path root = makeBenchRoot("extora_range_get", "64MiB");

  extora::storage_error error;
  const extora::object_store_options options = makeObjectStoreOptions(root, extora::storage_durability::relaxed);
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::abort();

  const extora::bucket_name bucket{"bench"};
  error = store->create_bucket(bucket);
  if (failed(error)) std::abort();

  const extora::object_key key{"object"};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = static_cast<std::uint64_t>(source.size());
  MemoryObjectReader reader{source};
  extora::put_object_result putResult;
  error = store->put_object(bucket, key, reader, metadata, putResult, putOptions);
  if (failed(error)) std::abort();

  std::vector<std::byte> buffer(rangeReadSize);
  std::uint64_t offset = 0;
  constexpr std::uint64_t offsetLimit = rangeObjectSize - rangeReadSize;
  for ([[maybe_unused]] auto _ : state) {
    extora::open_object_options getOptions;
    getOptions.range = extora::byte_range{extora::byte_range_type::offset_length, offset, rangeReadSize};

    MemoryObjectWriter writer{buffer};
    extora::open_object_result result;
    error = readObject(*store, bucket, key, writer, getOptions, result);
    if (failed(error)) std::abort();

    offset = (offset + rangeStride) % offsetLimit;
    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rangeReadSize));

  store.reset();
  std::filesystem::remove_all(root);
}

} // namespace

} // namespace extoraBenchmark

namespace {

const bool registeredBenchmarks = []() {
  extoraBenchmark::registerObjectSizes("Read/PlainFile", extoraBenchmark::plainFileRead);
  extoraBenchmark::registerObjectSizes("Read/Object/Cached", extoraBenchmark::cachedObjectRead);
  extoraBenchmark::registerObjectSizes("Read/Object/Uncached", extoraBenchmark::uncachedObjectRead);
  extoraBenchmark::registerObjectSizes("Read/Object/Limited", extoraBenchmark::limitedObjectRead);
  extoraBenchmark::configureObjectBenchmark(
      benchmark::RegisterBenchmark("Read/Object/Range4KiB/64MiB", extoraBenchmark::objectRangeRead));
#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
  extoraBenchmark::configureObjectBenchmark(benchmark::RegisterBenchmark("Read/Object/Cached/VerifyChecksum/1GiB",
                                                                         extoraBenchmark::verifiedCachedObjectRead));
#if defined(__linux__)
  extoraBenchmark::configureObjectBenchmark(
      benchmark::RegisterBenchmark("Read/PlainFile/Evicted/1GiB", extoraBenchmark::evictedPlainFileRead));
  extoraBenchmark::configureObjectBenchmark(
      benchmark::RegisterBenchmark("Read/Object/Evicted/1GiB", extoraBenchmark::evictedObjectRead));
#endif // defined(__linux__)
  extoraBenchmark::registerMaxExtentSizeBenchmarks();
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
  return true;
}();

} // namespace
