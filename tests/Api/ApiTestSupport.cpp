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

#include "ApiTestSupport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "PublicTestSupport.h"

namespace extoraTest {

std::string apiChecksumValue(std::string_view text)
{
  std::uint64_t value = 0;
  for (const char ch : text)
    value += static_cast<unsigned char>(ch);

  return std::to_string(value);
}

extora::storage_error ApiHasher::update(const std::byte* data, std::size_t size)
{
  for (std::size_t i = 0; i < size; ++i)
    m_value += static_cast<unsigned char>(data[i]);

  return {};
}

extora::storage_error ApiHasher::finish(std::string& value)
{
  value = std::to_string(m_value);
  return {};
}

std::unique_ptr<extora::hasher>
ApiHasherFactory::create_hasher(const extora::checksum_algorithm_name& checksumAlgorithm, extora::storage_error& error)
{
  error = {};
  if (checksumAlgorithm.value != "test-sum") return {};

  return std::make_unique<ApiHasher>();
}

bool ApiHasherFactory::can_combine_checksums(const extora::checksum_algorithm_name& checksumAlgorithm) const
{
  return checksumAlgorithm.value == "test-sum";
}

extora::storage_error ApiHasherFactory::combine_checksums(const extora::checksum_algorithm_name& checksumAlgorithm,
                                                          const std::vector<std::string_view>& partChecksums,
                                                          std::string& value)
{
  value.clear();
  if (checksumAlgorithm.value != "test-sum") {
    return extora::make_error(extora::storage_error_code::unsupported_checksum_type,
                              "unsupported test composite checksum");
  }

  std::string encodedChecksums;
  for (const std::string_view checksum : partChecksums)
    encodedChecksums.append(checksum.data(), checksum.size());
  value = apiChecksumValue(encodedChecksums) + '-' + std::to_string(partChecksums.size());
  return {};
}

extora::object_read_result FailingReader::read(std::byte*, std::size_t)
{
  extora::object_read_result result;
  result.error = extora::make_error(extora::storage_error_code::source_failure, "reader failed");
  return result;
}

extora::storage_error FailingWriter::write(const std::byte*, std::size_t)
{
  return extora::make_error(extora::storage_error_code::sink_failure, "writer failed");
}

extora::storage_error putText(extora::object_store& store, std::string_view bucket, std::string_view key,
                              std::string_view text, const extora::object_metadata& metadata,
                              const extora::put_object_options& options, extora::put_object_result* result)
{
  VectorReader reader{bytesFromString(text), 3};
  extora::put_object_result& output = result == nullptr ? ignoredPutResult() : *result;
  return store.put_object(extora::bucket_name{std::string{bucket}}, extora::object_key{std::string{key}}, reader,
                          metadata, output, options);
}

extora::storage_error readText(extora::object_store& store, std::string_view bucket, std::string_view key,
                               std::string& text, const extora::open_object_options& options,
                               extora::open_object_result* result)
{
  VectorWriter writer;
  extora::open_object_result& output = result == nullptr ? ignoredOpenObjectResult() : *result;
  const extora::storage_error error = readObject(store, extora::bucket_name{std::string{bucket}},
                                                 extora::object_key{std::string{key}}, writer, options, output);
  text = stringFromBytes(writer.bytes());
  return error;
}

void ObjectStoreApiTest::SetUp()
{
  m_root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = m_root;
  options.segment_capacity = 1024 * 1024;
  options.custom_hasher_factory = m_hasherFactory;

  extora::storage_error error;
  m_store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_NE(m_store, nullptr);
  ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"photos"})));
}

} // namespace extoraTest
