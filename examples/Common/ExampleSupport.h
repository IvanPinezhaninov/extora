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

#ifndef EXTORA_EXAMPLE_SUPPORT_H
#define EXTORA_EXAMPLE_SUPPORT_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <extora/extora.h>

namespace extoraExample {

inline int printError(const char* operation, const extora::storage_error& error)
{
  std::cerr << operation << " failed: " << error.message << '\n';
  return EXIT_FAILURE;
}

inline std::vector<std::byte> bytesFromString(std::string_view text)
{
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());
  for (const char ch : text)
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  return bytes;
}

inline std::string stringFromBytes(const std::vector<std::byte>& bytes)
{
  std::string text;
  text.reserve(bytes.size());
  for (const std::byte byte : bytes)
    text.push_back(static_cast<char>(byte));
  return text;
}

inline std::string formatTime(const std::chrono::system_clock::time_point& value)
{
  const std::time_t time = std::chrono::system_clock::to_time_t(value);
  std::tm utcTime;
#if defined(_WIN32)
  if (::gmtime_s(&utcTime, &time) != 0) return "invalid time";
#else
  if (::gmtime_r(&time, &utcTime) == nullptr) return "invalid time";
#endif // defined(_WIN32)

  std::array<char, 32> buffer;
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S UTC", &utcTime) == 0) return "invalid time";
  return buffer.data();
}

class MemoryReader final : public extora::object_reader {
public:
  explicit MemoryReader(std::vector<std::byte> data)
    : m_data{std::move(data)}
  {}

  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    const std::size_t remaining = m_data.size() - m_offset;
    const std::size_t bytesToCopy = std::min(remaining, size);
    if (bytesToCopy > 0) {
      std::copy(m_data.begin() + static_cast<std::ptrdiff_t>(m_offset),
                m_data.begin() + static_cast<std::ptrdiff_t>(m_offset + bytesToCopy), data);
      m_offset += bytesToCopy;
    }

    extora::object_read_result result;
    result.bytes_read = bytesToCopy;
    result.end_of_stream = m_offset == m_data.size();
    return result;
  }

private:
  std::vector<std::byte> m_data;
  std::size_t m_offset = 0;
};

class MemoryWriter final : public extora::object_writer {
public:
  extora::storage_error write(const std::byte* data, std::size_t size) override
  {
    m_data.insert(m_data.end(), data, data + size);
    return {};
  }

  const std::vector<std::byte>& bytes() const
  {
    return m_data;
  }

  std::string text() const
  {
    return stringFromBytes(m_data);
  }

private:
  std::vector<std::byte> m_data;
};

inline extora::storage_error readObject(extora::object_store& store, const extora::bucket_name& bucket,
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

inline extora::storage_error createExampleBucket(extora::object_store& store)
{
  extora::storage_error error = store.create_bucket(extora::bucket_name{"examples"});
  if (error.code == extora::storage_error_code::bucket_already_exists) return {};
  return error;
}

} // namespace extoraExample

#endif // EXTORA_EXAMPLE_SUPPORT_H
