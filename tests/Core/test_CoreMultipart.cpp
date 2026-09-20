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

#include <chrono>
#include <cstdint>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

TEST_F(StoreCoreTest, MultipartUploadCompletesSinglePartObject)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"large-object"};
  extora::object_metadata metadata;
  metadata.content_type = "application/octet-stream";

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, metadata, createResult)));
  ASSERT_FALSE(createResult.upload_id.value.empty());

  VectorReader reader{bytesFromString("multipart-body"), 4};
  extora::upload_part_result partResult;
  const extora::storage_error uploadError =
      m_core->upload_part(bucket, key, createResult.upload_id, 1, reader, partResult);
  ASSERT_TRUE(succeeded(uploadError)) << uploadError.message;
  EXPECT_FALSE(partResult.etag.empty());
  EXPECT_EQ(partResult.content_length, 14);

  extora::multipart_part_list parts;
  ASSERT_TRUE(succeeded(m_core->list_parts(bucket, key, createResult.upload_id, parts)));
  ASSERT_EQ(parts.parts.size(), 1);
  EXPECT_EQ(parts.parts[0].part_number, 1);
  EXPECT_EQ(parts.parts[0].etag, partResult.etag);
  EXPECT_EQ(partResult.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  ASSERT_TRUE(parts.parts[0].checksum.has_value());
  EXPECT_EQ(parts.parts[0].checksum->value, partResult.checksum.value);

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, partResult.etag, std::nullopt});
  extora::put_object_result completeResult;
  const extora::storage_error completeError =
      m_core->complete_multipart_upload(bucket, key, createResult.upload_id, completeOptions, completeResult);
  ASSERT_TRUE(succeeded(completeError)) << completeError.message;
  EXPECT_FALSE(completeResult.etag.empty());
  EXPECT_FALSE(completeResult.version_id.value.empty());
  EXPECT_EQ(completeResult.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);

  VectorWriter writer;
  extora::open_object_result getResult;
  ASSERT_TRUE(succeeded(readObject(*m_core, bucket, key, writer, extora::open_object_options{}, getResult)));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "multipart-body");
  EXPECT_EQ(getResult.object.content_length, 14);
  EXPECT_EQ(getResult.object.content_type, metadata.content_type);
  ASSERT_TRUE(getResult.object.checksum.has_value());
  EXPECT_EQ(getResult.object.checksum->value, completeResult.checksum.value);

  EXPECT_EQ(m_core->list_parts(bucket, key, createResult.upload_id, parts).code,
            extora::storage_error_code::multipart_upload_not_found);
}

TEST_F(StoreCoreTest, MultipartUploadReplacesPartWithSameNumber)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"replace-part"};

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, createResult)));

  VectorReader firstReader{bytesFromString("first"), 8};
  extora::upload_part_result firstPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 1, firstReader, firstPart)));

  VectorReader secondReader{bytesFromString("second"), 8};
  extora::upload_part_result secondPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 1, secondReader, secondPart)));
  EXPECT_NE(firstPart.etag, secondPart.etag);

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, secondPart.etag, std::nullopt});
  ASSERT_TRUE(succeeded(
      m_core->complete_multipart_upload(bucket, key, createResult.upload_id, completeOptions, ignoredPutResult())));

  VectorWriter writer;
  ASSERT_TRUE(
      succeeded(readObject(*m_core, bucket, key, writer, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "second");
}

TEST_F(StoreCoreTest, MultipartUploadCompletesMultipleParts)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"multi-part"};

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, createResult)));

  std::vector<std::byte> firstData(5 * 1024 * 1024, std::byte{0x61});
  VectorReader firstReader{firstData, 64 * 1024};
  extora::upload_part_result firstPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 1, firstReader, firstPart)));

  VectorReader secondReader{bytesFromString("tail"), 4};
  extora::upload_part_result secondPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 2, secondReader, secondPart)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, firstPart.etag, std::nullopt});
  completeOptions.parts.push_back(extora::completed_multipart_part{2, secondPart.etag, std::nullopt});
  ASSERT_TRUE(succeeded(
      m_core->complete_multipart_upload(bucket, key, createResult.upload_id, completeOptions, ignoredPutResult())));

  extora::head_object_options headOptions;
  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(bucket, key, info, headOptions)));
  EXPECT_EQ(info.content_length, firstData.size() + 4);

  extora::open_object_options getOptions;
  getOptions.range = extora::byte_range{extora::byte_range_type::suffix, 0, 4};
  VectorWriter writer;
  extora::open_object_result getResult;
  ASSERT_TRUE(succeeded(readObject(*m_core, bucket, key, writer, getOptions, getResult)));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "tail");
}

TEST_F(StoreCoreTest, MultipartUploadAbortRemovesUploadAndParts)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"abort-object"};

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, createResult)));

  VectorReader reader{bytesFromString("part"), 4};
  ASSERT_TRUE(
      succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 1, reader, ignoredUploadPartResult())));

  ASSERT_TRUE(succeeded(m_core->abort_multipart_upload(bucket, key, createResult.upload_id)));

  extora::multipart_part_list parts;
  EXPECT_EQ(m_core->list_parts(bucket, key, createResult.upload_id, parts).code,
            extora::storage_error_code::multipart_upload_not_found);
  VectorWriter writer;
  EXPECT_EQ(readObject(*m_core, bucket, key, writer, extora::open_object_options{}, ignoredOpenObjectResult()).code,
            extora::storage_error_code::object_not_found);
}

TEST_F(StoreCoreTest, MultipartUploadValidatesCompletionParts)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"validate-complete"};

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, createResult)));

  VectorReader reader{bytesFromString("small"), 8};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 2, reader, part)));

  extora::complete_multipart_upload_options missingPart;
  missingPart.parts.push_back(extora::completed_multipart_part{1, part.etag, std::nullopt});
  EXPECT_EQ(
      m_core->complete_multipart_upload(bucket, key, createResult.upload_id, missingPart, ignoredPutResult()).code,
      extora::storage_error_code::invalid_part);

  extora::complete_multipart_upload_options wrongEtag;
  wrongEtag.parts.push_back(extora::completed_multipart_part{2, "wrong", std::nullopt});
  EXPECT_EQ(m_core->complete_multipart_upload(bucket, key, createResult.upload_id, wrongEtag, ignoredPutResult()).code,
            extora::storage_error_code::invalid_part);

  extora::complete_multipart_upload_options ordered;
  ordered.parts.push_back(extora::completed_multipart_part{2, part.etag, std::nullopt});
  ordered.parts.push_back(extora::completed_multipart_part{1, part.etag, std::nullopt});
  EXPECT_EQ(m_core->complete_multipart_upload(bucket, key, createResult.upload_id, ordered, ignoredPutResult()).code,
            extora::storage_error_code::invalid_part_order);
}

TEST_F(StoreCoreTest, MultipartUploadRejectsSmallNonLastPart)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"small-non-last"};

  extora::create_multipart_upload_result createResult;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, createResult)));

  VectorReader firstReader{bytesFromString("first"), 8};
  extora::upload_part_result firstPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 1, firstReader, firstPart)));

  VectorReader secondReader{bytesFromString("second"), 8};
  extora::upload_part_result secondPart;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, createResult.upload_id, 2, secondReader, secondPart)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, firstPart.etag, std::nullopt});
  completeOptions.parts.push_back(extora::completed_multipart_part{2, secondPart.etag, std::nullopt});
  EXPECT_EQ(
      m_core->complete_multipart_upload(bucket, key, createResult.upload_id, completeOptions, ignoredPutResult()).code,
      extora::storage_error_code::part_too_small);
}

TEST_F(StoreCoreTest, CompletingSubsetAbandonsUnselectedParts)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"selected-parts"};
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, upload)));

  VectorReader selectedReader{bytesFromString("selected"), 8};
  extora::upload_part_result selected;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, upload.upload_id, 1, selectedReader, selected)));
  VectorReader unusedReader{bytesFromString("unused"), 6};
  extora::upload_part_result unused;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, upload.upload_id, 2, unusedReader, unused)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, selected.etag, std::nullopt});
  ASSERT_TRUE(
      succeeded(m_core->complete_multipart_upload(bucket, key, upload.upload_id, completeOptions, ignoredPutResult())));

  const std::string databasePath = joinPath(m_root, "index.sqlite3");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);
}

TEST_F(StoreCoreTest, BucketWithMultipartUploadIsNotEmpty)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"pending"};
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, upload)));

  EXPECT_EQ(m_core->delete_bucket(bucket).code, extora::storage_error_code::bucket_not_empty);
  ASSERT_TRUE(succeeded(m_core->abort_multipart_upload(bucket, key, upload.upload_id)));
  EXPECT_TRUE(succeeded(m_core->delete_bucket(bucket)));
}

TEST_F(StoreCoreTest, ListMultipartUploadsReturnsOpenUploads)
{
  extora::create_multipart_upload_result first;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"a/first"},
                                                        extora::object_metadata{}, first)));
  extora::create_multipart_upload_result second;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"a/b/second"},
                                                        extora::object_metadata{}, second)));

  extora::list_multipart_uploads_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  extora::multipart_upload_list uploads;
  ASSERT_TRUE(succeeded(m_core->list_multipart_uploads(extora::bucket_name{"photos"}, uploads, options)));
  ASSERT_EQ(uploads.uploads.size(), 1);
  EXPECT_EQ(uploads.uploads[0].key.value, "a/first");
  EXPECT_EQ(uploads.common_prefixes, (std::vector<std::string>{"a/b/"}));
}

TEST_F(StoreCoreTest, CapsPartPagesAtTheS3Maximum)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"large-upload"};
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, upload)));

  for (std::uint32_t partNumber = 1; partNumber <= extora::max_list_page_size + 1; ++partNumber) {
    extora::core::indexed_multipart_part part;
    part.info.part_number = partNumber;
    part.info.content_length = 0;
    part.info.etag = "empty-etag";
    part.info.created_at = std::chrono::system_clock::now();
    part.internal_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "empty-value"};
    ASSERT_TRUE(succeeded(m_index->store_multipart_part(upload.upload_id, bucket, key, part)));
  }

  extora::list_parts_options options;
  options.max_parts = extora::max_list_page_size + 100;
  extora::multipart_part_list parts;
  ASSERT_TRUE(succeeded(m_core->list_parts(bucket, key, upload.upload_id, parts, options)));
  EXPECT_EQ(parts.parts.size(), extora::max_list_page_size);
  EXPECT_TRUE(parts.is_truncated);
  ASSERT_TRUE(parts.next_part_number_marker.has_value());
  EXPECT_EQ(*parts.next_part_number_marker, extora::max_list_page_size);
}

} // namespace extoraTest
