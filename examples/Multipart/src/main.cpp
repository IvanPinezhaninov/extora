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
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "ExampleSupport.h"

namespace {

// Every part except the last one must contain at least the configured minimum.
constexpr std::uint64_t firstPartSize = extora::default_multipart_min_part_size;

class PatternReader final : public extora::object_reader {
public:
  PatternReader(std::byte value, std::uint64_t size)
    : m_value{value}
    , m_remaining{size}
  {}

  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    const std::size_t bytesToWrite = static_cast<std::size_t>(std::min(m_remaining, static_cast<std::uint64_t>(size)));
    std::fill(data, data + bytesToWrite, m_value);
    m_remaining -= static_cast<std::uint64_t>(bytesToWrite);
    return {bytesToWrite, m_remaining == 0, {}};
  }

private:
  std::byte m_value;
  std::uint64_t m_remaining;
};

class MultipartUploadGuard {
public:
  MultipartUploadGuard(extora::object_store& store, extora::bucket_name bucket, extora::object_key key,
                       extora::multipart_upload_id uploadId)
    : m_store{store}
    , m_bucket{std::move(bucket)}
    , m_key{std::move(key)}
    , m_uploadId{std::move(uploadId)}
  {}

  ~MultipartUploadGuard()
  {
    if (m_active) static_cast<void>(m_store.abort_multipart_upload(m_bucket, m_key, m_uploadId));
  }

  MultipartUploadGuard(const MultipartUploadGuard&) = delete;
  MultipartUploadGuard& operator=(const MultipartUploadGuard&) = delete;

  void dismiss()
  {
    m_active = false;
  }

private:
  extora::object_store& m_store;
  extora::bucket_name m_bucket;
  extora::object_key m_key;
  extora::multipart_upload_id m_uploadId;
  bool m_active = true;
};

class ExpectedMultipartWriter final : public extora::object_writer {
public:
  explicit ExpectedMultipartWriter(std::string tail)
    : m_tail{std::move(tail)}
  {}

  extora::storage_error write(const std::byte* data, std::size_t size) override
  {
    for (std::size_t index = 0; index < size; ++index) {
      const std::uint64_t position = m_bytesWritten + static_cast<std::uint64_t>(index);
      if (position >= firstPartSize + m_tail.size())
        return {extora::storage_error_code::sink_failure, "multipart object is longer than expected"};
      const std::byte expected =
          position < firstPartSize ? static_cast<std::byte>('A')
                                   : static_cast<std::byte>(m_tail[static_cast<std::size_t>(position - firstPartSize)]);
      if (data[index] != expected)
        return {extora::storage_error_code::sink_failure, "multipart object content does not match"};
    }
    m_bytesWritten += static_cast<std::uint64_t>(size);
    return {};
  }

  bool complete() const
  {
    return m_bytesWritten == firstPartSize + m_tail.size();
  }

private:
  std::string m_tail;
  std::uint64_t m_bytesWritten = 0;
};

extora::storage_error uploadPart(extora::object_store& store, const extora::bucket_name& bucket,
                                 const extora::object_key& key, const extora::multipart_upload_id& uploadId,
                                 std::uint32_t partNumber, extora::object_reader& reader, std::uint64_t contentLength,
                                 extora::upload_part_result& result)
{
  extora::upload_part_options options;
  options.expected_content_length = contentLength;

  return store.upload_part(bucket, key, uploadId, partNumber, reader, result, options);
}

} // namespace

int main()
{
  using namespace extoraExample;

  const extora::bucket_name bucket{"examples"};
  const extora::object_key key{"multipart.bin"};
  const std::string tail = "multipart upload complete\n";

  extora::object_store_options storeOptions;
  storeOptions.root_directory = "extora-multipart-example-store";
  storeOptions.segment_capacity = 16 * 1024 * 1024;
  storeOptions.max_extent_size = 1024 * 1024;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(storeOptions, error);
  if (failed(error)) return printError("open data store", error);

  error = createExampleBucket(*store);
  if (failed(error)) return printError("create bucket", error);

  extora::object_metadata metadata;
  metadata.content_type = "application/octet-stream";
  metadata.custom_metadata.push_back({"example", "multipart"});

  extora::create_multipart_upload_result createResult;
  error = store->create_multipart_upload(bucket, key, metadata, createResult);
  if (failed(error)) return printError("create multipart upload", error);

  MultipartUploadGuard uploadGuard{*store, bucket, key, createResult.upload_id};

  PatternReader firstReader{static_cast<std::byte>('A'), firstPartSize};
  extora::upload_part_result firstPart;
  error = uploadPart(*store, bucket, key, createResult.upload_id, 1, firstReader, firstPartSize, firstPart);
  if (failed(error)) return printError("upload first part", error);

  MemoryReader secondReader{bytesFromString(tail)};
  extora::upload_part_result secondPart;
  error = uploadPart(*store, bucket, key, createResult.upload_id, 2, secondReader, tail.size(), secondPart);
  if (failed(error)) return printError("upload second part", error);

  extora::multipart_part_list uploadedParts;
  error = store->list_parts(bucket, key, createResult.upload_id, uploadedParts);
  if (failed(error)) return printError("list uploaded parts", error);
  if (uploadedParts.is_truncated || uploadedParts.parts.size() != 2) {
    std::cerr << "Expected exactly two uploaded parts\n";
    return EXIT_FAILURE;
  }

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back({1, firstPart.etag, firstPart.checksum.value});
  completeOptions.parts.push_back({2, secondPart.etag, secondPart.checksum.value});

  for (const extora::multipart_part_info& part : uploadedParts.parts) {
    std::cout << "Uploaded part " << part.part_number << ": " << part.content_length << " bytes, ETag " << part.etag
              << '\n';
  }

  extora::put_object_result completeResult;
  error = store->complete_multipart_upload(bucket, key, createResult.upload_id, completeOptions, completeResult);
  if (failed(error)) return printError("complete multipart upload", error);
  uploadGuard.dismiss();

  ExpectedMultipartWriter writer{tail};
  extora::open_object_result getResult;
  error = readObject(*store, bucket, key, writer, extora::open_object_options{}, getResult);
  if (failed(error)) return printError("read completed object", error);
  if (!writer.complete()) {
    std::cerr << "Completed object has an unexpected size\n";
    return EXIT_FAILURE;
  }

  std::cout << "Completed " << key.value << ": " << getResult.object.content_length << " bytes, ETag "
            << completeResult.etag << '\n';
  return EXIT_SUCCESS;
}
