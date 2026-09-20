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
#include <array>
#include <chrono>

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

class StalledReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte*, std::size_t) override
  {
    return {};
  }
};

class TerminalErrorReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    const std::size_t bytesRead = (std::min)(size, std::size_t{4});
    std::fill_n(data, bytesRead, std::byte{0x2a});
    return extora::object_read_result{
        bytesRead, false, extora::make_error(extora::storage_error_code::source_failure, "terminal source error")};
  }
};

class OversizedResultReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte*, std::size_t size) override
  {
    return extora::object_read_result{size + 1, true, {}};
  }
};

} // namespace

TEST_F(ObjectStoreApiTest, StreamsEmptySmallAndMultiChunkObjects)
{
  for (const std::string& body : {std::string{}, std::string{"small"}, std::string(128 * 1024, 'x')}) {
    const std::string key = "object-" + std::to_string(body.size());
    extora::put_object_options options;
    options.expected_content_length = body.size();
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, body, {}, options)));

    std::string stored;
    ASSERT_TRUE(succeeded(readText(*m_store, "photos", key, stored)));
    EXPECT_EQ(stored, body);
  }
}

TEST_F(ObjectStoreApiTest, RoundTripsMetadataThroughPutHeadAndGet)
{
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  metadata.cache_control = "max-age=3600";
  metadata.content_disposition = "attachment; filename=metadata.txt";
  metadata.content_encoding = "gzip";
  metadata.content_language = "en";
  metadata.expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{1893456000}};
  metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "api-test"});
  extora::put_object_result putResult;
  ASSERT_TRUE(succeeded(
      putText(*m_store, "photos", "metadata", "contents", metadata, extora::put_object_options{}, &putResult)));

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"metadata"}, info)));
  EXPECT_EQ(info.key.value, "metadata");
  EXPECT_EQ(info.etag, putResult.etag);
  EXPECT_EQ(info.version_id.value, extora::null_version_id);
  EXPECT_EQ(info.content_length, 8u);
  EXPECT_EQ(info.content_type, metadata.content_type);
  EXPECT_EQ(info.cache_control, metadata.cache_control);
  EXPECT_EQ(info.content_disposition, metadata.content_disposition);
  EXPECT_EQ(info.content_encoding, metadata.content_encoding);
  EXPECT_EQ(info.content_language, metadata.content_language);
  EXPECT_EQ(info.expires_at, metadata.expires_at);
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(info.checksum->value, putResult.checksum.value);
  ASSERT_EQ(info.custom_metadata.size(), 1u);
  EXPECT_EQ(info.custom_metadata[0].value, "api-test");

  std::string stored;
  extora::open_object_result getResult;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "metadata", stored, extora::open_object_options{}, &getResult)));
  EXPECT_EQ(getResult.object.etag, putResult.etag);
  EXPECT_EQ(getResult.object.cache_control, metadata.cache_control);
  EXPECT_EQ(getResult.object.expires_at, metadata.expires_at);
  ASSERT_TRUE(getResult.object.checksum.has_value());
  EXPECT_EQ(getResult.object.checksum->value, putResult.checksum.value);
  EXPECT_FALSE(getResult.byte_range.has_value());
}

TEST_F(ObjectStoreApiTest, OpensMetadataBeforeReadingAnImmutableSnapshot)
{
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "snapshot", "original", metadata)));

  extora::open_object_result result;
  ASSERT_TRUE(succeeded(m_store->open_object(extora::bucket_name{"photos"}, extora::object_key{"snapshot"}, result)));
  ASSERT_NE(result.reader, nullptr);
  EXPECT_EQ(result.object.content_length, 8u);
  EXPECT_EQ(result.object.content_type, metadata.content_type);
  EXPECT_FALSE(result.byte_range.has_value());

  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "snapshot", "replacement")));
  extora::delete_object_result deleteResult;
  ASSERT_TRUE(
      succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"snapshot"}, deleteResult)));
  ASSERT_TRUE(succeeded(m_store->reclaim_storage(ignoredReclaimResult())));

  std::array<std::byte, 32> buffer;
  const extora::object_read_result readResult = result.reader->read(buffer.data(), buffer.size());
  ASSERT_TRUE(succeeded(readResult.error));
  EXPECT_TRUE(readResult.end_of_stream);
  EXPECT_EQ(readResult.bytes_read, 8u);
  EXPECT_EQ(stringFromBytes(std::vector<std::byte>{buffer.begin(), buffer.begin() + readResult.bytes_read}),
            "original");
}

TEST_F(ObjectStoreApiTest, OverwritesAndDeletesAnUnversionedObject)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "mutable", "first")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "mutable", "second")));

  std::string stored;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "mutable", stored)));
  EXPECT_EQ(stored, "second");

  extora::delete_object_result result;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"mutable"}, result)));
  EXPECT_FALSE(result.is_delete_marker);
  EXPECT_EQ(result.version_id.value, extora::null_version_id);
  EXPECT_EQ(readText(*m_store, "photos", "mutable", stored).code, extora::storage_error_code::object_not_found);
  EXPECT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"mutable"}, result)));
}

TEST_F(ObjectStoreApiTest, DoesNotPublishAContentLengthMismatch)
{
  extora::put_object_options options;
  options.expected_content_length = 4;
  EXPECT_EQ(putText(*m_store, "photos", "wrong-length", "five!", {}, options).code,
            extora::storage_error_code::source_failure);

  extora::object_info info;
  EXPECT_EQ(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"wrong-length"}, info).code,
            extora::storage_error_code::object_not_found);
}

TEST_F(ObjectStoreApiTest, ReportsAddressAndStreamFailures)
{
  EXPECT_EQ(putText(*m_store, "missing", "object", "data").code, extora::storage_error_code::bucket_not_found);
  EXPECT_EQ(putText(*m_store, "photos", "", "data").code, extora::storage_error_code::invalid_object_key);
  EXPECT_EQ(putText(*m_store, "photos", std::string(extora::max_object_key_size + 1, 'k'), "data").code,
            extora::storage_error_code::invalid_object_key);
  EXPECT_EQ(putText(*m_store, "photos", std::string{"invalid\0key", 11}, "data").code,
            extora::storage_error_code::invalid_object_key);

  extora::open_object_result openResult;
  EXPECT_EQ(m_store->open_object(extora::bucket_name{"photos"}, extora::object_key{}, openResult).code,
            extora::storage_error_code::invalid_object_key);

  FailingReader reader;
  EXPECT_EQ(m_store
                ->put_object(extora::bucket_name{"photos"}, extora::object_key{"reader-error"}, reader,
                             extora::object_metadata{}, ignoredPutResult())
                .code,
            extora::storage_error_code::source_failure);

  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "writer-error", "data")));
  FailingWriter writer;
  EXPECT_EQ(readObject(*m_store, extora::bucket_name{"photos"}, extora::object_key{"writer-error"}, writer,
                       extora::open_object_options{}, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::sink_failure);
}

TEST_F(ObjectStoreApiTest, RejectsAReaderThatMakesNoProgress)
{
  StalledReader reader;
  const extora::storage_error error =
      m_store->put_object(extora::bucket_name{"photos"}, extora::object_key{"stalled-reader"}, reader,
                          extora::object_metadata{}, ignoredPutResult());
  EXPECT_EQ(error.code, extora::storage_error_code::source_failure);
  EXPECT_EQ(error.message, "object reader made no progress");

  extora::object_info info;
  EXPECT_EQ(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"stalled-reader"}, info).code,
            extora::storage_error_code::object_not_found);
}

TEST_F(ObjectStoreApiTest, AcceptsValidBytesAlongsideATerminalReaderError)
{
  TerminalErrorReader reader;
  const extora::storage_error error =
      m_store->put_object(extora::bucket_name{"photos"}, extora::object_key{"terminal-reader"}, reader,
                          extora::object_metadata{}, ignoredPutResult());
  EXPECT_EQ(error.code, extora::storage_error_code::source_failure);
  EXPECT_EQ(error.message, "terminal source error");

  extora::object_info info;
  EXPECT_EQ(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"terminal-reader"}, info).code,
            extora::storage_error_code::object_not_found);
}

TEST_F(ObjectStoreApiTest, RejectsAReaderThatReportsMoreBytesThanRequested)
{
  OversizedResultReader reader;
  const extora::storage_error error =
      m_store->put_object(extora::bucket_name{"photos"}, extora::object_key{"oversized-reader"}, reader,
                          extora::object_metadata{}, ignoredPutResult());
  EXPECT_EQ(error.code, extora::storage_error_code::source_failure);
  EXPECT_EQ(error.message, "object reader returned too many bytes");
}

TEST_F(ObjectStoreApiTest, ReportsAndReclaimsStorage)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "first payload")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "replacement payload")));

  extora::reclamation_estimate before;
  ASSERT_TRUE(succeeded(m_store->get_reclamation_estimate(before)));
  EXPECT_GT(before.reclaimable_extent_count, 0u);
  EXPECT_GT(before.reclaimable_bytes, 0u);
  extora::reclaim_storage_result reclaimed;
  ASSERT_TRUE(succeeded(m_store->reclaim_storage(reclaimed)));
  EXPECT_EQ(reclaimed.reclaimed_extent_count, before.reclaimable_extent_count);
  EXPECT_EQ(reclaimed.reclaimed_bytes, before.reclaimable_bytes);
  EXPECT_EQ(reclaimed.remaining_reclaimable_extent_count, 0u);
  EXPECT_EQ(reclaimed.remaining_reclaimable_bytes, 0u);

  extora::reclaim_storage_result repeated;
  ASSERT_TRUE(succeeded(m_store->reclaim_storage(repeated)));
  EXPECT_EQ(repeated.reclaimed_extent_count, 0u);
  EXPECT_EQ(repeated.reclaimed_bytes, 0u);

  extora::reclamation_estimate after;
  ASSERT_TRUE(succeeded(m_store->get_reclamation_estimate(after)));
  EXPECT_EQ(after.reclaimable_extent_count, 0u);
  EXPECT_EQ(after.reclaimable_bytes, 0u);
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "object", text)));
  EXPECT_EQ(text, "replacement payload");
}

} // namespace extoraTest
