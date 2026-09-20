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
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
** USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include "BenchmarkSupport.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "extora/extora.h"

namespace extoraBenchmark {

namespace {

struct ObjectSize4KiB {
  static constexpr std::size_t value = 4 * 1024;
};

struct ObjectSize64KiB {
  static constexpr std::size_t value = 64 * 1024;
};

struct ObjectSize1MiB {
  static constexpr std::size_t value = 1024 * 1024;
};

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
struct ObjectSize64MiB {
  static constexpr std::size_t value = 64 * 1024 * 1024;
};
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

class ConcurrentStoreFixture : public benchmark::Fixture {
protected:
  template<typename Function>
  void setUpShared(Function function)
  {
    const std::lock_guard<std::mutex> lock{m_fixtureMutex};
    if (m_setupReady) return;

    function();
    m_setupReady = true;
  }

  void tearDownShared(const benchmark::State& state)
  {
    std::unique_lock<std::mutex> lock{m_fixtureMutex};
    ++m_finishedThreads;
    if (m_finishedThreads == static_cast<std::size_t>(state.threads())) {
      closeStore();
      m_finishedThreads = 0;
      m_setupReady = false;
      lock.unlock();
      m_fixtureCondition.notify_all();
      return;
    }

    m_fixtureCondition.wait(lock, [this]() { return !m_setupReady; });
  }

  void openStore(const char* name, std::size_t cacheCapacity)
  {
    m_root = makeBenchRoot(name, "shared");
    extora::object_store_options options = makeObjectStoreOptions(m_root, extora::storage_durability::balanced);
    options.object_lookup_cache_capacity = cacheCapacity;

    extora::storage_error error;
    m_store = extora::open_object_store(options, error);
    if (failed(error) || !m_store) std::abort();

    error = m_store->create_bucket(m_bucket);
    if (failed(error)) std::abort();
  }

  void putObject(const extora::object_key& key, const std::vector<std::byte>& data)
  {
    extora::object_metadata metadata;

    MemoryObjectReader reader{data};
    extora::put_object_options options;
    options.expected_content_length = static_cast<std::uint64_t>(data.size());
    options.dedup = extora::dedup_mode::disabled;

    extora::put_object_result result;
    const extora::storage_error error = m_store->put_object(m_bucket, key, reader, metadata, result, options);
    if (failed(error)) std::abort();
  }

  void closeStore()
  {
    m_store.reset();
    std::filesystem::remove_all(m_root);
  }

  std::filesystem::path m_root;
  std::unique_ptr<extora::object_store> m_store;
  extora::bucket_name m_bucket{"bench"};

private:
  std::condition_variable m_fixtureCondition;
  std::mutex m_fixtureMutex;
  std::size_t m_finishedThreads = 0;
  bool m_setupReady = false;
};

template<typename ObjectSize>
class ConcurrentReadFixture : public ConcurrentStoreFixture {
public:
  void SetUp(const benchmark::State&) override
  {
    setUpShared([this]() {
      openStore("concurrent_read", extora::default_object_lookup_cache_capacity);
      m_source = makeBuffer(ObjectSize::value);
      putObject(m_key, m_source);

      std::vector<std::byte> buffer(m_source.size());
      MemoryObjectWriter writer{buffer};
      extora::open_object_result result;
      const extora::storage_error error =
          readObject(*m_store, m_bucket, m_key, writer, extora::open_object_options{}, result);
      if (failed(error)) std::abort();
    });
  }

  void TearDown(const benchmark::State& state) override
  {
    tearDownShared(state);
  }

protected:
  std::vector<std::byte> m_source;
  extora::object_key m_key{"object"};
};

class ConcurrentCachedHeadFixture : public ConcurrentStoreFixture {
public:
  void SetUp(const benchmark::State&) override
  {
    setUpShared([this]() {
      openStore("concurrent_head_cached", extora::default_object_lookup_cache_capacity);
      const std::vector<std::byte> source = makeBuffer(1);
      putObject(m_key, source);

      // Fill the cache used by head_object().
      std::vector<std::byte> buffer(source.size());
      MemoryObjectWriter writer{buffer};
      extora::open_object_result result;
      const extora::storage_error error =
          readObject(*m_store, m_bucket, m_key, writer, extora::open_object_options{}, result);
      if (failed(error)) std::abort();
    });
  }

  void TearDown(const benchmark::State& state) override
  {
    tearDownShared(state);
  }

protected:
  extora::object_key m_key{"object"};
};

class ConcurrentUncachedHeadFixture : public ConcurrentStoreFixture {
public:
  void SetUp(const benchmark::State&) override
  {
    setUpShared([this]() {
      openStore("concurrent_head_uncached", 0);
      const std::vector<std::byte> source = makeBuffer(1);
      putObject(m_key, source);
    });
  }

  void TearDown(const benchmark::State& state) override
  {
    tearDownShared(state);
  }

protected:
  extora::object_key m_key{"object"};
};

template<typename ObjectSize>
class ConcurrentWriteFixture : public ConcurrentStoreFixture {
public:
  void SetUp(const benchmark::State&) override
  {
    setUpShared([this]() {
      openStore("concurrent_write", extora::default_object_lookup_cache_capacity);
      m_source = makeBuffer(ObjectSize::value);
    });
  }

  void TearDown(const benchmark::State& state) override
  {
    tearDownShared(state);
  }

protected:
  std::vector<std::byte> m_source;
};

BENCHMARK_TEMPLATE_METHOD_F(ConcurrentReadFixture, GetObject)(benchmark::State& state)
{
  std::vector<std::byte> buffer(this->m_source.size());
  for ([[maybe_unused]] auto _ : state) {
    MemoryObjectWriter writer{buffer};
    extora::open_object_result result;
    const extora::storage_error error =
        readObject(*this->m_store, this->m_bucket, this->m_key, writer, extora::open_object_options{}, result);
    if (failed(error)) std::abort();
    benchmark::DoNotOptimize(buffer.data());
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * this->m_source.size()));
}

BENCHMARK_DEFINE_F(ConcurrentCachedHeadFixture, HeadObject)(benchmark::State& state)
{
  for ([[maybe_unused]] auto _ : state) {
    extora::object_info info;
    const extora::storage_error error = m_store->head_object(m_bucket, m_key, info);
    if (failed(error)) std::abort();
    benchmark::DoNotOptimize(info.content_length);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}

BENCHMARK_DEFINE_F(ConcurrentUncachedHeadFixture, HeadObject)(benchmark::State& state)
{
  for ([[maybe_unused]] auto _ : state) {
    extora::object_info info;
    const extora::storage_error error = m_store->head_object(m_bucket, m_key, info);
    if (failed(error)) std::abort();
    benchmark::DoNotOptimize(info.content_length);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}

BENCHMARK_TEMPLATE_METHOD_F(ConcurrentWriteFixture, PutObjectBalanced)(benchmark::State& state)
{
  extora::object_metadata metadata;

  extora::put_object_options options;
  options.expected_content_length = static_cast<std::uint64_t>(this->m_source.size());
  options.conditions.if_none_match_etag = "*";
  options.dedup = extora::dedup_mode::disabled;

  std::uint64_t objectIndex = 0;
  const std::string keyPrefix = "thread-" + std::to_string(state.thread_index()) + "-object-";
  for ([[maybe_unused]] auto _ : state) {
    const extora::object_key key{keyPrefix + std::to_string(objectIndex++)};
    MemoryObjectReader reader{this->m_source};
    extora::put_object_result result;
    const extora::storage_error error =
        this->m_store->put_object(this->m_bucket, key, reader, metadata, result, options);
    if (failed(error)) {
      state.SkipWithError(error.message.c_str());
      break;
    }
    benchmark::DoNotOptimize(objectIndex);
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * this->m_source.size()));
}

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentReadFixture, GetObject, ObjectSize4KiB)
    ->Name("Concurrent/Read/Object/Cached/4KiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentReadFixture, GetObject, ObjectSize64KiB)
    ->Name("Concurrent/Read/Object/Cached/64KiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentReadFixture, GetObject, ObjectSize1MiB)
    ->Name("Concurrent/Read/Object/Cached/1MiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_REGISTER_F(ConcurrentCachedHeadFixture, HeadObject)
    ->Name("Concurrent/Metadata/Head/Cached")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_REGISTER_F(ConcurrentUncachedHeadFixture, HeadObject)
    ->Name("Concurrent/Metadata/Head/Uncached")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentWriteFixture, PutObjectBalanced, ObjectSize4KiB)
    ->Name("Concurrent/Write/Object/Balanced/4KiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentWriteFixture, PutObjectBalanced, ObjectSize64KiB)
    ->Name("Concurrent/Write/Object/Balanced/64KiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentWriteFixture, PutObjectBalanced, ObjectSize1MiB)
    ->Name("Concurrent/Write/Object/Balanced/1MiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
BENCHMARK_TEMPLATE_INSTANTIATE_F(ConcurrentWriteFixture, PutObjectBalanced, ObjectSize64MiB)
    ->Name("Concurrent/Write/Object/Balanced/64MiB")
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond)
    ->MinTime(1.0);
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

} // namespace

} // namespace extoraBenchmark
