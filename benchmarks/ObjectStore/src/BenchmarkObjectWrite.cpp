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

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "extora/extora.h"

namespace extoraBenchmark {

namespace {

struct ExtentSizeCase {
  std::uint64_t bytes;
  const char* name;
};

struct DurabilityCase {
  extora::storage_durability value;
  const char* name;
};

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
constexpr std::array<ExtentSizeCase, 10> extentSizeCases{{
    {256 * 1024, "256KiB"},
    {512 * 1024, "512KiB"},
    {1024 * 1024, "1MiB"},
    {2 * 1024 * 1024, "2MiB"},
    {4 * 1024 * 1024, "4MiB"},
    {8 * 1024 * 1024, "8MiB"},
    {16 * 1024 * 1024, "16MiB"},
    {32 * 1024 * 1024, "32MiB"},
    {64 * 1024 * 1024, "64MiB"},
    {128 * 1024 * 1024, "128MiB"},
}};

constexpr std::array<DurabilityCase, 2> durabilityCases{{
    {extora::storage_durability::balanced, "Balanced"},
    {extora::storage_durability::relaxed, "Relaxed"},
}};
#else
constexpr std::array<ExtentSizeCase, 3> extentSizeCases{{
    {4 * 1024 * 1024, "4MiB"},
    {8 * 1024 * 1024, "8MiB"},
    {16 * 1024 * 1024, "16MiB"},
}};

constexpr std::array<DurabilityCase, 1> durabilityCases{{
    {extora::storage_durability::relaxed, "Relaxed"},
}};
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

void benchPlainFileWrite(benchmark::State& state, std::int64_t objectSizeValue, const char* name, const char* sizeName,
                         bool syncFile)
{
  const std::size_t objectSize = static_cast<std::size_t>(objectSizeValue);
  const std::vector<std::byte> buffer = makeBuffer(objectSize);
  const std::filesystem::path root = makeBenchRoot(name, sizeName);

  std::uint64_t index = 0;

  for ([[maybe_unused]] auto _ : state) {
    const std::filesystem::path path = root / ("object-" + std::to_string(index++) + ".bin");
    writePlainFile(path, buffer, syncFile);
    benchmark::DoNotOptimize(index);
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));

  std::filesystem::remove_all(root);
}

void benchExtoraPutObject(benchmark::State& state, std::int64_t objectSizeValue, const char* name, const char* sizeName,
                          extora::storage_durability durability, bool writeContentLength,
                          bool concurrencyLimited = false,
                          std::uint64_t maxExtentSize = extora::default_max_extent_size)
{
  const std::size_t objectSize = static_cast<std::size_t>(objectSizeValue);
  const std::vector<std::byte> buffer = makeBuffer(objectSize);
  const std::filesystem::path root = makeBenchRoot(name, sizeName);

  extora::storage_error error;
  extora::object_store_options options = makeObjectStoreOptions(root, durability);
  options.max_extent_size = maxExtentSize;
  if (concurrencyLimited) options.max_concurrent_writes = 64;

  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);

  if (failed(error) || !store) std::abort();

  extora::bucket_name bucket;
  bucket.value = "bench";

  error = store->create_bucket(bucket);
  if (failed(error)) std::abort();

  std::uint64_t index = 0;

  for ([[maybe_unused]] auto _ : state) {
    extora::object_key key;
    key.value = "object-" + std::to_string(++index);

    extora::object_metadata metadata;

    extora::put_object_options putOptions;
    putOptions.conditions.if_none_match_etag = "*";
    if (writeContentLength) putOptions.expected_content_length = static_cast<std::uint64_t>(buffer.size());

    MemoryObjectReader reader{buffer};

    extora::put_object_result putResult;
    error = store->put_object(bucket, key, reader, metadata, putResult, putOptions);
    if (failed(error)) std::abort();

    benchmark::DoNotOptimize(index);
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * objectSize));

  store.reset();
  std::filesystem::remove_all(root);
}

void syncedPlainFileWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchPlainFileWrite(state, objectSize, "plain_write_synced", sizeName, true);
}

void unsyncedPlainFileWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchPlainFileWrite(state, objectSize, "plain_write_unsynced", sizeName, false);
}

void strictKnownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_strict", sizeName, extora::storage_durability::strict, true);
}

void balancedKnownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_balanced", sizeName, extora::storage_durability::balanced, true);
}

void relaxedKnownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_relaxed", sizeName, extora::storage_durability::relaxed, true);
}

void limitedRelaxedKnownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_relaxed_limited", sizeName, extora::storage_durability::relaxed,
                       true, true);
}

void strictUnknownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_unknown_strict", sizeName, extora::storage_durability::strict,
                       false);
}

void balancedUnknownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_unknown_balanced", sizeName, extora::storage_durability::balanced,
                       false);
}

void relaxedUnknownLengthWrite(benchmark::State& state, std::int64_t objectSize, const char* sizeName)
{
  benchExtoraPutObject(state, objectSize, "extora_put_unknown_relaxed", sizeName, extora::storage_durability::relaxed,
                       false);
}

void benchMaxExtentSize(benchmark::State& state, extora::storage_durability durability, bool writeContentLength,
                        std::uint64_t maxExtentSize, const char* durabilityName, const char* extentName)
{
  constexpr std::int64_t objectSize = 64 * 1024 * 1024;
  const std::string name =
      std::string{"extent_"} + durabilityName + '_' + extentName + (writeContentLength ? "_known" : "_unknown");
  benchExtoraPutObject(state, objectSize, name.c_str(), "64MiB", durability, writeContentLength, false, maxExtentSize);
}

void registerMaxExtentSizeBenchmark(const DurabilityCase& durability, bool writeContentLength,
                                    const ExtentSizeCase& extentSize)
{
  const std::string lengthName = writeContentLength ? "KnownLength" : "UnknownLength";
  const std::string benchmarkName =
      "Write/Object/" + std::string{durability.name} + '/' + lengthName + "/Extent" + extentSize.name + "/64MiB";
  configureObjectBenchmark(benchmark::RegisterBenchmark(benchmarkName.c_str(), [durability, writeContentLength,
                                                                                extentSize](benchmark::State& state) {
    benchMaxExtentSize(state, durability.value, writeContentLength, extentSize.bytes, durability.name, extentSize.name);
  }));
}

void registerMaxExtentSizeBenchmarks()
{
  for (const DurabilityCase& durability : durabilityCases) {
    for (const ExtentSizeCase& extentSize : extentSizeCases) {
      registerMaxExtentSizeBenchmark(durability, true, extentSize);
      registerMaxExtentSizeBenchmark(durability, false, extentSize);
    }
  }
}

} // namespace

} // namespace extoraBenchmark

namespace {

const bool registeredBenchmarks = []() {
  extoraBenchmark::registerObjectSizes("Write/PlainFile/Synced", extoraBenchmark::syncedPlainFileWrite);
  extoraBenchmark::registerObjectSizes("Write/PlainFile/Unsynced", extoraBenchmark::unsyncedPlainFileWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Strict/KnownLength", extoraBenchmark::strictKnownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Balanced/KnownLength", extoraBenchmark::balancedKnownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Relaxed/KnownLength", extoraBenchmark::relaxedKnownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Relaxed/KnownLength/Limited",
                                       extoraBenchmark::limitedRelaxedKnownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Strict/UnknownLength", extoraBenchmark::strictUnknownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Balanced/UnknownLength",
                                       extoraBenchmark::balancedUnknownLengthWrite);
  extoraBenchmark::registerObjectSizes("Write/Object/Relaxed/UnknownLength",
                                       extoraBenchmark::relaxedUnknownLengthWrite);
  extoraBenchmark::registerMaxExtentSizeBenchmarks();
  return true;
}();

} // namespace
