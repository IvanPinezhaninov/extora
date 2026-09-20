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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>

#include "ExampleSupport.h"

namespace {

constexpr std::uint64_t kibibyte = 1024;

std::string_view operationTypeName(extora::operation_type type)
{
  switch (type) {
  case extora::operation_type::put_object:
    return "put_object";
  case extora::operation_type::open_object:
    return "open_object";
  case extora::operation_type::upload_part:
    return "upload_part";
  case extora::operation_type::compact_storage:
    return "compact_storage";
  }
  return "unknown";
}

std::string_view operationStageName(extora::operation_stage stage)
{
  switch (stage) {
  case extora::operation_stage::started:
    return "started";
  case extora::operation_stage::progress:
    return "progress";
  case extora::operation_stage::completed:
    return "completed";
  case extora::operation_stage::failed:
    return "failed";
  }
  return "unknown";
}

class OperationLoggingObserver final : public extora::operation_observer {
public:
  void on_operation_event(const extora::operation_event& event) override
  {
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::clog << "[extora]" << " id=" << event.operation_id << " type=" << operationTypeName(event.type)
              << " stage=" << operationStageName(event.stage) << " bucket=" << event.bucket << " key=" << event.key
              << " bytes=" << event.bytes_processed;

    if (event.total_bytes.has_value())
      std::clog << '/' << *event.total_bytes;
    else
      std::clog << "/unknown";

    if (event.read_offset.has_value()) std::clog << " read_offset=" << *event.read_offset;

    if (event.error.has_value()) {
      std::clog << " error_code=" << static_cast<unsigned int>(event.error->code) << " error=\"" << event.error->message
                << '"';
    }

    std::clog << '\n';
  }

private:
  std::mutex m_mutex;
};

class PatternReader final : public extora::object_reader {
public:
  explicit PatternReader(std::uint64_t totalSize)
    : m_totalSize{totalSize}
  {}

  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    constexpr std::size_t sourceChunkSize = 32 * kibibyte;
    const std::uint64_t remaining = m_totalSize - m_offset;
    const std::size_t bytesToWrite =
        static_cast<std::size_t>(std::min<std::uint64_t>(remaining, std::min(size, sourceChunkSize)));

    for (std::size_t index = 0; index < bytesToWrite; ++index)
      data[index] = static_cast<std::byte>('a' + static_cast<int>((m_offset + index) % 26));

    m_offset += bytesToWrite;
    return {bytesToWrite, m_offset == m_totalSize, {}};
  }

private:
  std::uint64_t m_totalSize = 0;
  std::uint64_t m_offset = 0;
};

class CountingWriter final : public extora::object_writer {
public:
  extora::storage_error write(const std::byte*, std::size_t size) override
  {
    m_bytesWritten += size;
    return {};
  }

  std::uint64_t bytesWritten() const
  {
    return m_bytesWritten;
  }

private:
  std::uint64_t m_bytesWritten = 0;
};

class FailingWriter final : public extora::object_writer {
public:
  explicit FailingWriter(std::uint64_t byteLimit)
    : m_byteLimit{byteLimit}
  {}

  extora::storage_error write(const std::byte*, std::size_t size) override
  {
    if (m_bytesWritten + size > m_byteLimit)
      return extora::make_error(extora::storage_error_code::sink_failure, "example output stopped accepting data");

    m_bytesWritten += size;
    return {};
  }

private:
  std::uint64_t m_byteLimit = 0;
  std::uint64_t m_bytesWritten = 0;
};

} // namespace

int main()
{
  using namespace extoraExample;

  const std::filesystem::path root = "extora-observability-example-store";
  std::error_code filesystemError;
  std::filesystem::remove_all(root, filesystemError);
  if (filesystemError) {
    std::cerr << "remove old example store failed: " << filesystemError.message() << '\n';
    return EXIT_FAILURE;
  }

  const std::shared_ptr<OperationLoggingObserver> observer = std::make_shared<OperationLoggingObserver>();
  extora::object_store_options options;
  options.root_directory = root;
  options.max_extent_size = 64 * kibibyte;
  options.observer = observer;
  options.operation_progress_interval_bytes = 64 * kibibyte;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error)) return printError("open data store", error);

  error = createExampleBucket(*store);
  if (failed(error)) return printError("create bucket", error);

  constexpr std::uint64_t objectSize = 256 * kibibyte;
  PatternReader reader{objectSize};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = objectSize;

  extora::put_object_result putResult;
  error = store->put_object(extora::bucket_name{"examples"}, extora::object_key{"large-object.bin"}, reader, metadata,
                            putResult, putOptions);
  if (failed(error)) return printError("put object", error);

  CountingWriter writer;
  extora::open_object_result getResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"large-object.bin"}, writer,
                     extora::open_object_options{}, getResult);
  if (failed(error)) return printError("get object", error);

  constexpr std::uint64_t rangeOffset = 64 * kibibyte;
  constexpr std::uint64_t rangeLength = 96 * kibibyte;
  CountingWriter rangeWriter;
  extora::open_object_options rangeOptions;
  rangeOptions.range = extora::byte_range{extora::byte_range_type::offset_length, rangeOffset, rangeLength};
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"large-object.bin"}, rangeWriter,
                     rangeOptions, getResult);
  if (failed(error)) return printError("get object range", error);

  FailingWriter failingWriter{128 * kibibyte};
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"large-object.bin"}, failingWriter,
                     extora::open_object_options{}, getResult);
  if (error.code != extora::storage_error_code::sink_failure) {
    if (failed(error)) return printError("get object with failing sink", error);
    std::cerr << "failing writer unexpectedly accepted the complete object\n";
    return EXIT_FAILURE;
  }

  std::cout << "Successful download: " << writer.bytesWritten()
            << " bytes\nRange download: " << rangeWriter.bytesWritten() << " bytes from offset " << rangeOffset
            << "\nExpected sink failure was reported through "
               "the observer and returned to the caller.\n";
  return EXIT_SUCCESS;
}
