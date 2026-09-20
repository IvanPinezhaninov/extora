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
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, CompletesAMultipartUploadAndPublishesMetadata)
{
  extora::object_metadata metadata;
  metadata.content_type = "application/octet-stream";
  metadata.cache_control = "no-cache";
  metadata.content_disposition = "attachment";
  metadata.content_encoding = "gzip";
  metadata.content_language = "de";
  metadata.expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{1893456000}};
  metadata.custom_metadata.push_back(extora::metadata_entry{"source", "multipart"});
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         metadata, upload)));

  VectorReader reader{bytesFromString("multipart-body"), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, part)));
  EXPECT_EQ(part.content_length, 14u);
  EXPECT_EQ(part.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_NE(part.created_at, std::chrono::system_clock::time_point{});

  extora::complete_multipart_upload_options options;
  options.parts.push_back(extora::completed_multipart_part{1, part.etag, std::nullopt});
  extora::put_object_result result;
  ASSERT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                           upload.upload_id, options, result)));
  EXPECT_EQ(result.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);

  std::string text;
  extora::open_object_result getResult;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "archive", text, {}, &getResult)));
  EXPECT_EQ(text, "multipart-body");
  EXPECT_EQ(getResult.object.content_type, metadata.content_type);
  EXPECT_EQ(getResult.object.cache_control, metadata.cache_control);
  EXPECT_EQ(getResult.object.content_disposition, metadata.content_disposition);
  EXPECT_EQ(getResult.object.content_encoding, metadata.content_encoding);
  EXPECT_EQ(getResult.object.content_language, metadata.content_language);
  EXPECT_EQ(getResult.object.expires_at, metadata.expires_at);
  ASSERT_TRUE(getResult.object.checksum.has_value());
  EXPECT_EQ(getResult.object.checksum->value, result.checksum.value);
  ASSERT_EQ(getResult.object.custom_metadata.size(), 1u);
}

TEST_F(ObjectStoreApiTest, ReplacesAPartWithTheSameNumber)
{
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
  VectorReader firstReader{bytesFromString("first"), 3};
  extora::upload_part_result first;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, firstReader, first)));
  VectorReader secondReader{bytesFromString("second"), 3};
  extora::upload_part_result second;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, secondReader, second)));
  EXPECT_NE(first.etag, second.etag);

  extora::complete_multipart_upload_options options;
  options.parts.push_back(extora::completed_multipart_part{1, second.etag, std::nullopt});
  ASSERT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                           upload.upload_id, options, ignoredPutResult())));
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "archive", text)));
  EXPECT_EQ(text, "second");
}

TEST_F(ObjectStoreApiTest, ListsUploadedPartsWithPagination)
{
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
  for (std::uint32_t partNumber = 1; partNumber <= 3; ++partNumber) {
    VectorReader reader{bytesFromString(std::to_string(partNumber)), 1};
    ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                               upload.upload_id, partNumber, reader, ignoredUploadPartResult())));
  }

  extora::list_parts_options options;
  options.max_parts = 2;
  extora::multipart_part_list first;
  ASSERT_TRUE(succeeded(m_store->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, first, options)));
  ASSERT_EQ(first.parts.size(), 2u);
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_part_number_marker.has_value());
  EXPECT_EQ(*first.next_part_number_marker, 2u);

  options.part_number_marker = *first.next_part_number_marker;
  extora::multipart_part_list second;
  ASSERT_TRUE(succeeded(m_store->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, second, options)));
  ASSERT_EQ(second.parts.size(), 1u);
  EXPECT_EQ(second.parts[0].part_number, 3u);
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_part_number_marker.has_value());
}

TEST_F(ObjectStoreApiTest, ListsCompletedObjectPartsForTheResolvedVersion)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));

  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));

  std::vector<std::byte> firstData(5 * 1024 * 1024, std::byte{'a'});
  VectorReader firstReader{std::move(firstData), 256 * 1024};
  extora::upload_part_result firstPart;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, firstReader, firstPart)));
  VectorReader secondReader{bytesFromString("tail"), 2};
  extora::upload_part_result secondPart;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 3, secondReader, secondPart)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, firstPart.etag, firstPart.checksum.value});
  completeOptions.parts.push_back(extora::completed_multipart_part{3, secondPart.etag, secondPart.checksum.value});
  extora::put_object_result completed;
  ASSERT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                           upload.upload_id, completeOptions, completed)));

  extora::list_object_parts_options listOptions;
  listOptions.max_parts = 1;
  extora::object_part_list firstPage;
  ASSERT_TRUE(succeeded(m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                   firstPage, listOptions)));
  EXPECT_EQ(firstPage.object.version_id.value, completed.version_id.value);
  EXPECT_EQ(firstPage.total_parts, 2u);
  ASSERT_EQ(firstPage.parts.size(), 1u);
  EXPECT_EQ(firstPage.parts[0].part_number, 1u);
  EXPECT_EQ(firstPage.parts[0].offset, 0u);
  EXPECT_EQ(firstPage.parts[0].content_length, 5u * 1024u * 1024u);
  EXPECT_EQ(firstPage.parts[0].checksum.value, firstPart.checksum.value);
  EXPECT_TRUE(firstPage.is_truncated);
  ASSERT_TRUE(firstPage.next_part_number_marker.has_value());
  EXPECT_EQ(*firstPage.next_part_number_marker, 1u);

  listOptions.part_number_marker = *firstPage.next_part_number_marker;
  extora::object_part_list secondPage;
  ASSERT_TRUE(succeeded(m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                   secondPage, listOptions)));
  EXPECT_EQ(secondPage.total_parts, 2u);
  ASSERT_EQ(secondPage.parts.size(), 1u);
  EXPECT_EQ(secondPage.parts[0].part_number, 3u);
  EXPECT_EQ(secondPage.parts[0].offset, 5u * 1024u * 1024u);
  EXPECT_EQ(secondPage.parts[0].content_length, 4u);
  EXPECT_EQ(secondPage.parts[0].checksum.value, secondPart.checksum.value);
  EXPECT_FALSE(secondPage.is_truncated);
  EXPECT_FALSE(secondPage.next_part_number_marker.has_value());

  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "archive", "replacement")));
  extora::object_part_list current;
  ASSERT_TRUE(
      succeeded(m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, current)));
  EXPECT_EQ(current.total_parts, 0u);
  EXPECT_TRUE(current.parts.empty());

  listOptions = {};
  listOptions.version_id = completed.version_id;
  extora::object_part_list retained;
  ASSERT_TRUE(succeeded(
      m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, retained, listOptions)));
  EXPECT_EQ(retained.total_parts, 2u);
  ASSERT_EQ(retained.parts.size(), 2u);

  extora::open_object_options readOptions;
  readOptions.version_id = completed.version_id;
  readOptions.range = extora::byte_range{extora::byte_range_type::offset_length, retained.parts[1].offset,
                                         retained.parts[1].content_length};
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "archive", text, readOptions)));
  EXPECT_EQ(text, "tail");
}

TEST_F(ObjectStoreApiTest, ListsMultipartUploadsByPrefixAndDelimiter)
{
  for (std::string_view key : {"a/first", "a/b/second", "a/b/third", "z/fourth"}) {
    extora::create_multipart_upload_result upload;
    ASSERT_TRUE(succeeded(m_store->create_multipart_upload(
        extora::bucket_name{"photos"}, extora::object_key{std::string{key}}, extora::object_metadata{}, upload)));
  }

  extora::list_multipart_uploads_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  extora::multipart_upload_list uploads;
  ASSERT_TRUE(succeeded(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, uploads, options)));
  ASSERT_EQ(uploads.uploads.size(), 1u);
  EXPECT_EQ(uploads.uploads[0].key.value, "a/first");
  EXPECT_EQ(uploads.uploads[0].checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(uploads.uploads[0].checksum_type, extora::object_checksum_type::full_object);
  EXPECT_EQ(uploads.common_prefixes, (std::vector<std::string>{"a/b/"}));
}

TEST_F(ObjectStoreApiTest, FiltersMultipartCommonPrefixesAtKeyMarker)
{
  for (std::string_view key : {"a/b/first", "a/b/zeta", "a/c/first"}) {
    extora::create_multipart_upload_result upload;
    ASSERT_TRUE(succeeded(m_store->create_multipart_upload(
        extora::bucket_name{"photos"}, extora::object_key{std::string{key}}, extora::object_metadata{}, upload)));
  }

  extora::list_multipart_uploads_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  options.key_marker = "a/b/middle";
  extora::multipart_upload_list result;
  ASSERT_TRUE(succeeded(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, result, options)));
  EXPECT_TRUE(result.uploads.empty());
  EXPECT_EQ(result.common_prefixes, (std::vector<std::string>{"a/c/"}));
}

TEST_F(ObjectStoreApiTest, RejectsInvalidMultipartUploadPageSizes)
{
  extora::list_multipart_uploads_options options;
  options.max_uploads = 0;
  extora::multipart_upload_list result;
  EXPECT_EQ(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, result, options).code,
            extora::storage_error_code::invalid_list_options);

  options.max_uploads = extora::max_list_page_size + 1;
  EXPECT_EQ(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, result, options).code,
            extora::storage_error_code::invalid_list_options);
}

TEST_F(ObjectStoreApiTest, PaginatesMultipartUploadsWithKeyAndUploadIdMarkers)
{
  extora::create_multipart_upload_result firstUpload;
  extora::create_multipart_upload_result secondUpload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, firstUpload)));
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, secondUpload)));

  extora::list_multipart_uploads_options options;
  options.max_uploads = 1;
  extora::multipart_upload_list first;
  ASSERT_TRUE(succeeded(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, first, options)));
  ASSERT_EQ(first.uploads.size(), 1u);
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_key_marker.has_value());
  ASSERT_TRUE(first.next_upload_id_marker.has_value());
  EXPECT_EQ(*first.next_key_marker, "archive");
  EXPECT_FALSE(first.next_upload_id_marker->value.empty());

  options.key_marker = *first.next_key_marker;
  options.upload_id_marker = *first.next_upload_id_marker;
  extora::multipart_upload_list second;
  ASSERT_TRUE(succeeded(m_store->list_multipart_uploads(extora::bucket_name{"photos"}, second, options)));
  ASSERT_EQ(second.uploads.size(), 1u);
  EXPECT_EQ(second.uploads[0].key.value, "archive");
  EXPECT_NE(second.uploads[0].upload_id.value, first.uploads[0].upload_id.value);
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_key_marker.has_value());
  EXPECT_FALSE(second.next_upload_id_marker.has_value());
}

TEST_F(ObjectStoreApiTest, AbortsAMultipartUpload)
{
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
  VectorReader reader{bytesFromString("part"), 4};
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, ignoredUploadPartResult())));
  ASSERT_TRUE(succeeded(
      m_store->abort_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id)));

  extora::multipart_part_list parts;
  EXPECT_EQ(
      m_store->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, parts).code,
      extora::storage_error_code::multipart_upload_not_found);
}

TEST_F(ObjectStoreApiTest, RejectsUnsupportedMultipartChecksumOptions)
{
  extora::create_multipart_upload_options options;
  extora::create_multipart_upload_result result;

  options.checksum_algorithm.value.clear();
  EXPECT_EQ(m_store
                ->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"empty-algorithm"},
                                          extora::object_metadata{}, result, options)
                .code,
            extora::storage_error_code::unsupported_checksum_algorithm);

  options.checksum_algorithm = extora::checksum_algorithm_name{"unsupported"};
  EXPECT_EQ(m_store
                ->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"unsupported-algorithm"},
                                          extora::object_metadata{}, result, options)
                .code,
            extora::storage_error_code::unsupported_checksum_algorithm);

  options.checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  options.checksum_type = static_cast<extora::object_checksum_type>(255);
  EXPECT_EQ(m_store
                ->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"unsupported-type"},
                                          extora::object_metadata{}, result, options)
                .code,
            extora::storage_error_code::unsupported_checksum_type);
}

TEST_F(ObjectStoreApiTest, RejectsNonConsecutiveCompositeCompletion)
{
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  createOptions.checksum_type = extora::object_checksum_type::composite;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload, createOptions)));

  VectorReader reader{bytesFromString("part"), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 2, reader, part)));
  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{2, part.etag, part.checksum.value});
  EXPECT_EQ(m_store
                ->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, completeOptions, ignoredPutResult())
                .code,
            extora::storage_error_code::invalid_part_order);
}

TEST_F(ObjectStoreApiTest, ReportsVersionErrorsWhenListingCompletedObjectParts)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "archive", "body")));

  extora::delete_object_result marker;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"archive"}, marker)));

  extora::list_object_parts_options options;
  options.version_id = extora::object_version_id{"missing-version"};
  extora::object_part_list parts;
  EXPECT_EQ(
      m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, parts, options).code,
      extora::storage_error_code::object_version_not_found);

  options.version_id = marker.version_id;
  EXPECT_EQ(
      m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, parts, options).code,
      extora::storage_error_code::object_is_delete_marker);
}

TEST_F(ObjectStoreApiTest, ValidatesMultipartCompletionAndBucketDeletion)
{
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
  EXPECT_EQ(m_store->delete_bucket(extora::bucket_name{"photos"}).code, extora::storage_error_code::bucket_not_empty);

  VectorReader reader{bytesFromString("part"), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 2, reader, part)));
  extora::complete_multipart_upload_options missing;
  missing.parts.push_back(extora::completed_multipart_part{1, part.etag, std::nullopt});
  EXPECT_EQ(m_store
                ->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, missing, ignoredPutResult())
                .code,
            extora::storage_error_code::invalid_part);

  extora::complete_multipart_upload_options wrongEtag;
  wrongEtag.parts.push_back(extora::completed_multipart_part{2, "wrong", std::nullopt});
  EXPECT_EQ(m_store
                ->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, wrongEtag, ignoredPutResult())
                .code,
            extora::storage_error_code::invalid_part);
}

TEST_F(ObjectStoreApiTest, ValidatesMultipartIdentifiersPartNumbersAndTargets)
{
  extora::multipart_part_list parts;
  EXPECT_EQ(m_store
                ->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                             extora::multipart_upload_id{}, parts)
                .code,
            extora::storage_error_code::multipart_upload_not_found);

  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));

  for (const std::uint32_t partNumber : {0u, 10001u}) {
    VectorReader reader{bytesFromString("part"), 4};
    EXPECT_EQ(m_store
                  ->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id,
                                partNumber, reader, ignoredUploadPartResult())
                  .code,
              extora::storage_error_code::invalid_part);
  }

  VectorReader wrongTargetReader{bytesFromString("part"), 4};
  EXPECT_EQ(m_store
                ->upload_part(extora::bucket_name{"photos"}, extora::object_key{"other"}, upload.upload_id, 1,
                              wrongTargetReader, ignoredUploadPartResult())
                .code,
            extora::storage_error_code::multipart_upload_not_found);
}

} // namespace extoraTest
