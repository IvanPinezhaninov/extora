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

#ifndef EXTORA_BENCHMARK_SUPPORT_H
#define EXTORA_BENCHMARK_SUPPORT_H

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <extora/extora.h>

#ifndef EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
#define EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS 0
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

namespace extoraBenchmark {

constexpr std::int64_t size4KiB = 4 * 1024;
constexpr std::int64_t size64KiB = 64 * 1024;
constexpr std::int64_t size1MiB = 1024 * 1024;
constexpr std::int64_t size64MiB = 64 * 1024 * 1024;

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
constexpr std::int64_t size256MiB = 256ll * 1024ll * 1024ll;
constexpr std::int64_t size1GiB = 1024ll * 1024ll * 1024ll;
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS

benchmark::Benchmark* configureObjectBenchmark(benchmark::Benchmark* benchmark);

template<typename Function>
inline void registerObjectSizes(std::string_view name, Function function)
{
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/4KiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size4KiB, "4KiB"); }));
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/64KiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size64KiB, "64KiB"); }));
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/1MiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size1MiB, "1MiB"); }));
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/64MiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size64MiB, "64MiB"); }));

#if EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/256MiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size256MiB, "256MiB"); }));
  configureObjectBenchmark(
      benchmark::RegisterBenchmark((std::string{name} + "/1GiB").c_str(),
                                   [function](benchmark::State& state) { function(state, size1GiB, "1GiB"); }));
#endif // EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS
}

std::filesystem::path makeBenchRoot(std::string_view name, std::string_view sizeName);
std::vector<std::byte> makeBuffer(std::size_t size);

class MemoryObjectReader final : public extora::object_reader {
public:
  explicit MemoryObjectReader(const std::vector<std::byte>& data);

  extora::object_read_result read(std::byte* data, std::size_t size) override;

private:
  const std::vector<std::byte>& m_data;
  std::size_t m_offset = 0;
};

class MemoryObjectWriter final : public extora::object_writer {
public:
  explicit MemoryObjectWriter(std::vector<std::byte>& data);

  extora::storage_error write(const std::byte* data, std::size_t size) override;

private:
  std::vector<std::byte>& m_data;
  std::size_t m_offset = 0;
};

extora::storage_error readObject(extora::object_store& store, const extora::bucket_name& bucket,
                                 const extora::object_key& key, extora::object_writer& writer,
                                 const extora::open_object_options& options, extora::open_object_result& result);

void writePlainFile(const std::filesystem::path& path, const std::vector<std::byte>& buffer, bool syncFile);
void readPlainFile(const std::filesystem::path& path, std::vector<std::byte>& buffer);
extora::object_store_options makeObjectStoreOptions(const std::filesystem::path& root,
                                                    extora::storage_durability durability);

} // namespace extoraBenchmark

#endif // EXTORA_BENCHMARK_SUPPORT_H
