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

#include "PublicTestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <share.h>
#endif // defined(_MSC_VER)
#include <sys/stat.h>

namespace extoraTest {

namespace {

class TemporaryRootRegistry {
public:
  ~TemporaryRootRegistry()
  {
    for (const std::filesystem::path& root : m_roots) {
      std::error_code errorCode;
      std::filesystem::remove_all(root, errorCode);
    }
  }

  void addRoot(std::filesystem::path root)
  {
    m_roots.push_back(std::move(root));
  }

private:
  std::vector<std::filesystem::path> m_roots;
};

TemporaryRootRegistry& temporaryRootRegistry()
{
  static TemporaryRootRegistry registry;
  return registry;
}

} // namespace

std::string makeTempRoot()
{
  static unsigned int counter = 0;
  ++counter;

  const std::uint64_t timestamp =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("extora_test_" + std::to_string(timestamp) + "_" + std::to_string(counter));
  temporaryRootRegistry().addRoot(root);
  return root.string();
}

std::string joinPath(const std::filesystem::path& parent, const std::filesystem::path& child)
{
  return (parent / child).string();
}

bool regularFileExists(const std::filesystem::path& path)
{
#if defined(_WIN32)
  struct _stat64 info;
  if (::_wstat64(path.c_str(), &info) != 0) return false;

  return (info.st_mode & _S_IFREG) != 0;
#else
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) return false;

  return S_ISREG(info.st_mode);
#endif // defined(_WIN32)
}

bool overwriteFileByte(const std::filesystem::path& path, long offset, std::byte value)
{
#if defined(_MSC_VER)
  std::FILE* file = ::_wfsopen(path.c_str(), L"r+b", _SH_DENYNO);
#elif defined(_WIN32)
  std::FILE* file = ::_wfopen(path.c_str(), L"r+b");
#else
  std::FILE* file = std::fopen(path.c_str(), "r+b");
#endif // defined(_MSC_VER)
  if (file == nullptr) return false;

  if (std::fseek(file, offset, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }

  const unsigned char byte = static_cast<unsigned char>(value);
  const bool written = std::fwrite(&byte, 1, 1, file) == 1;
  std::fclose(file);
  return written;
}

std::vector<std::byte> bytesFromString(std::string_view text)
{
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());
  for (const char ch : text)
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  return bytes;
}

std::string stringFromBytes(const std::vector<std::byte>& bytes)
{
  std::string text;
  text.reserve(bytes.size());
  for (const std::byte byte : bytes)
    text.push_back(static_cast<char>(byte));
  return text;
}

VectorReader::VectorReader(std::vector<std::byte> data, std::size_t chunkSize)
  : m_data{std::move(data)}
  , m_chunkSize{chunkSize}
{}

extora::object_read_result VectorReader::read(std::byte* data, std::size_t size)
{
  const std::size_t remaining = m_data.size() - m_offset;
  const std::size_t requested = std::min(size, m_chunkSize);
  const std::size_t bytesToCopy = std::min(remaining, requested);
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

extora::storage_error VectorWriter::write(const std::byte* data, std::size_t size)
{
  m_bytes.insert(m_bytes.end(), data, data + size);
  return {};
}

const std::vector<std::byte>& VectorWriter::bytes() const
{
  return m_bytes;
}

extora::storage_error readObject(extora::object_store& store, const extora::bucket_name& bucket,
                                 const extora::object_key& key, extora::object_writer& writer,
                                 const extora::open_object_options& options, extora::open_object_result& result)
{
  extora::storage_error error = store.open_object(bucket, key, result, options);
  if (failed(error)) return error;

  std::array<std::byte, 64 * 1024> buffer;
  while (true) {
    const extora::object_read_result readResult = result.reader->read(buffer.data(), buffer.size());
    if (readResult.bytes_read > 0) {
      error = writer.write(buffer.data(), readResult.bytes_read);
      if (failed(error)) {
        result.reader.reset();
        return error;
      }
    }
    if (failed(readResult.error)) return readResult.error;
    if (readResult.end_of_stream) return {};
    if (readResult.bytes_read == 0) {
      result.reader.reset();
      return extora::make_error(extora::storage_error_code::backend_failure, "object reader made no progress");
    }
  }
}

extora::open_object_result& ignoredOpenObjectResult()
{
  static thread_local extora::open_object_result result;
  result = {};
  return result;
}

extora::put_object_result& ignoredPutResult()
{
  static thread_local extora::put_object_result result;
  result = {};
  return result;
}

extora::delete_object_result& ignoredDeleteResult()
{
  static thread_local extora::delete_object_result result;
  result = {};
  return result;
}

extora::upload_part_result& ignoredUploadPartResult()
{
  static thread_local extora::upload_part_result result;
  result = {};
  return result;
}

extora::reclaim_storage_result& ignoredReclaimResult()
{
  static thread_local extora::reclaim_storage_result result;
  result = {};
  return result;
}

} // namespace extoraTest
