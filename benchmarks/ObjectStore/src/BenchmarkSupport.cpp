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

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif // defined(_WIN32)

namespace extoraBenchmark {

benchmark::Benchmark* configureObjectBenchmark(benchmark::Benchmark* benchmark)
{
  benchmark->UseRealTime();
  benchmark->Unit(benchmark::kMillisecond);
  benchmark->MinTime(0.1);
  return benchmark;
}

std::filesystem::path makeBenchRoot(std::string_view name, std::string_view sizeName)
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();

  std::string directory{"extora_bench_"};
  directory.append(name.data(), name.size());
  directory += '_';
  directory.append(sizeName.data(), sizeName.size());
  directory += '_';
  directory += std::to_string(now);

  std::filesystem::path path = std::filesystem::temp_directory_path() / directory;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::vector<std::byte> makeBuffer(std::size_t size)
{
  std::vector<std::byte> buffer(size);

  std::uint32_t value = 0x12345678u;
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    value = value * 1664525u + 1013904223u;
    buffer[i] = static_cast<std::byte>((value >> 24u) & 0xffu);
  }

  return buffer;
}

MemoryObjectReader::MemoryObjectReader(const std::vector<std::byte>& data)
  : m_data{data}
{}

extora::object_read_result MemoryObjectReader::read(std::byte* data, std::size_t size)
{
  extora::object_read_result result;

  const std::size_t remaining = m_data.size() - m_offset;
  const std::size_t bytesToRead = remaining < size ? remaining : size;

  if (bytesToRead != 0) {
    std::memcpy(data, m_data.data() + m_offset, bytesToRead);
    m_offset += bytesToRead;
  }

  result.bytes_read = bytesToRead;
  result.end_of_stream = m_offset == m_data.size();
  return result;
}

MemoryObjectWriter::MemoryObjectWriter(std::vector<std::byte>& data)
  : m_data{data}
{}

extora::storage_error MemoryObjectWriter::write(const std::byte* data, std::size_t size)
{
  if (m_offset + size > m_data.size()) std::abort();

  std::memcpy(m_data.data() + m_offset, data, size);
  m_offset += size;
  return {};
}

extora::storage_error readObject(extora::object_store& store, const extora::bucket_name& bucket,
                                 const extora::object_key& key, extora::object_writer& writer,
                                 const extora::open_object_options& options, extora::open_object_result& result)
{
  extora::storage_error error = store.open_object(bucket, key, result, options);
  if (extora::failed(error)) return error;

  std::array<std::byte, 64 * 1024> buffer;
  while (true) {
    const extora::object_read_result readResult = result.reader->read(buffer.data(), buffer.size());
    if (readResult.bytes_read > 0) {
      error = writer.write(buffer.data(), readResult.bytes_read);
      if (extora::failed(error)) {
        result.reader.reset();
        return error;
      }
    }
    if (extora::failed(readResult.error)) return readResult.error;
    if (readResult.end_of_stream) return {};
    if (readResult.bytes_read == 0) {
      result.reader.reset();
      return extora::make_error(extora::storage_error_code::backend_failure, "object reader made no progress");
    }
  }
}

void writePlainFile(const std::filesystem::path& path, const std::vector<std::byte>& buffer, bool syncFile)
{
#if defined(_MSC_VER)
  FILE* file = nullptr;
  ::_wfopen_s(&file, path.c_str(), L"wb");
#elif defined(_WIN32)
  FILE* file = ::_wfopen(path.c_str(), L"wb");
#else
  FILE* file = std::fopen(path.c_str(), "wb");
#endif // defined(_MSC_VER)
  if (file == nullptr) std::abort();

  const std::size_t written = std::fwrite(buffer.data(), 1, buffer.size(), file);
  if (written != buffer.size()) {
    std::fclose(file);
    std::abort();
  }

  if (syncFile) {
    if (std::fflush(file) != 0) {
      std::fclose(file);
      std::abort();
    }

#if defined(_WIN32)
    if (::_commit(::_fileno(file)) != 0) {
#elif defined(__APPLE__)
    if (::fsync(::fileno(file)) != 0) {
#else
    if (::fdatasync(::fileno(file)) != 0) {
#endif // defined(_WIN32)
      std::fclose(file);
      std::abort();
    }
  }

  std::fclose(file);
}

void readPlainFile(const std::filesystem::path& path, std::vector<std::byte>& buffer)
{
#if defined(_MSC_VER)
  FILE* file = nullptr;
  ::_wfopen_s(&file, path.c_str(), L"rb");
#elif defined(_WIN32)
  FILE* file = ::_wfopen(path.c_str(), L"rb");
#else
  FILE* file = std::fopen(path.c_str(), "rb");
#endif // defined(_MSC_VER)
  if (file == nullptr) std::abort();

  const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
  if (bytesRead != buffer.size()) {
    std::fclose(file);
    std::abort();
  }

  std::fclose(file);
}

extora::object_store_options makeObjectStoreOptions(const std::filesystem::path& root,
                                                    extora::storage_durability durability)
{
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024ull * 1024ull * 1024ull;
  options.durability = durability;
  return options;
}

} // namespace extoraBenchmark
