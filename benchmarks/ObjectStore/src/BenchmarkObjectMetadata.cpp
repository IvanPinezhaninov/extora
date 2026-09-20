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

constexpr std::size_t headObjectSize = 8 * 1024 * 1024;
constexpr std::size_t statExtentSize = 64 * 1024;
constexpr std::size_t listedObjectCount = 256;

std::unique_ptr<extora::object_store> makeStore(const std::filesystem::path& root, std::uint64_t maxExtentSize,
                                                std::size_t cacheCapacity)
{
  extora::object_store_options options = makeObjectStoreOptions(root, extora::storage_durability::relaxed);
  options.max_extent_size = maxExtentSize;
  options.object_lookup_cache_capacity = cacheCapacity;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::abort();

  error = store->create_bucket(extora::bucket_name{"bench"});
  if (failed(error)) std::abort();
  return store;
}

void putObject(extora::object_store& store, const extora::object_key& key, const std::vector<std::byte>& data)
{
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = static_cast<std::uint64_t>(data.size());
  MemoryObjectReader reader{data};
  extora::put_object_result putResult;
  const extora::storage_error error =
      store.put_object(extora::bucket_name{"bench"}, key, reader, metadata, putResult, options);
  if (failed(error)) std::abort();
}

void benchHeadObject(benchmark::State& state, bool cacheEnabled)
{
  const std::filesystem::path root = makeBenchRoot(cacheEnabled ? "stat_cached" : "stat_uncached", "8MiB");
  std::unique_ptr<extora::object_store> store =
      makeStore(root, statExtentSize, cacheEnabled ? extora::default_object_lookup_cache_capacity : 0);
  const std::vector<std::byte> source = makeBuffer(headObjectSize);
  const extora::object_key key{"object"};
  putObject(*store, key, source);

  if (cacheEnabled) {
    std::vector<std::byte> buffer(source.size());
    MemoryObjectWriter writer{buffer};
    extora::open_object_result result;
    const extora::storage_error error =
        readObject(*store, extora::bucket_name{"bench"}, key, writer, extora::open_object_options{}, result);
    if (failed(error)) std::abort();
  }

  for ([[maybe_unused]] auto _ : state) {
    extora::object_info info;
    const extora::storage_error error = store->head_object(extora::bucket_name{"bench"}, key, info);
    if (failed(error)) std::abort();
    benchmark::DoNotOptimize(info.content_length);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
  store.reset();
  std::filesystem::remove_all(root);
}

void cachedHeadObject(benchmark::State& state)
{
  benchHeadObject(state, true);
}

void uncachedHeadObject(benchmark::State& state)
{
  benchHeadObject(state, false);
}

void listObjects256(benchmark::State& state)
{
  const std::filesystem::path root = makeBenchRoot("list_objects", "256");
  std::unique_ptr<extora::object_store> store =
      makeStore(root, extora::default_max_extent_size, extora::default_object_lookup_cache_capacity);
  const std::vector<std::byte> source(1, std::byte{0x2a});

  for (std::size_t index = 0; index < listedObjectCount; ++index)
    putObject(*store, extora::object_key{"object-" + std::to_string(index)}, source);

  extora::list_objects_options options;
  options.max_keys = listedObjectCount;
  for ([[maybe_unused]] auto _ : state) {
    extora::object_list result;
    const extora::storage_error error = store->list_objects(extora::bucket_name{"bench"}, result, options);
    if (failed(error) || result.objects.size() != listedObjectCount) std::abort();
    benchmark::DoNotOptimize(result.objects.data());
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * listedObjectCount));
  store.reset();
  std::filesystem::remove_all(root);
}

benchmark::Benchmark* configureMetadataBenchmark(benchmark::Benchmark* benchmark)
{
  benchmark->UseRealTime();
  benchmark->Unit(benchmark::kMicrosecond);
  benchmark->MinTime(0.1);
  return benchmark;
}

} // namespace

} // namespace extoraBenchmark

namespace {

const bool registeredBenchmarks = []() {
  extoraBenchmark::configureMetadataBenchmark(
      benchmark::RegisterBenchmark("Metadata/Head/Cached", extoraBenchmark::cachedHeadObject));
  extoraBenchmark::configureMetadataBenchmark(
      benchmark::RegisterBenchmark("Metadata/Head/Uncached", extoraBenchmark::uncachedHeadObject));
  extoraBenchmark::configureMetadataBenchmark(
      benchmark::RegisterBenchmark("Metadata/List/256Objects", extoraBenchmark::listObjects256));
  return true;
}();

} // namespace
