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

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, CopiesAcrossBucketsAndPreservesMetadata)
{
  ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"archive"})));
  extora::object_metadata metadata;
  metadata.content_type = "text/source";
  metadata.cache_control = "max-age=60";
  metadata.content_encoding = "gzip";
  metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "source"});
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "copy body", metadata)));

  extora::copy_object_result result;
  ASSERT_TRUE(succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                             extora::bucket_name{"archive"}, extora::object_key{"target"}, result)));
  EXPECT_FALSE(result.etag.empty());
  EXPECT_EQ(result.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);

  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "archive", "target", text)));
  EXPECT_EQ(text, "copy body");
  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"archive"}, extora::object_key{"target"}, info)));
  EXPECT_EQ(info.content_type, metadata.content_type);
  EXPECT_EQ(info.cache_control, metadata.cache_control);
  EXPECT_EQ(info.content_encoding, metadata.content_encoding);
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->value, result.checksum.value);
  ASSERT_EQ(info.custom_metadata.size(), 1u);
  EXPECT_EQ(info.custom_metadata[0].value, "source");
}

TEST_F(ObjectStoreApiTest, ReplacesMetadataWhileCopying)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "copy body")));
  extora::copy_object_options options;
  options.replace_metadata = true;
  options.metadata.content_type = "text/replaced";
  options.metadata.content_disposition = "inline";
  options.metadata.content_language = "fr";
  options.metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "target"});
  extora::copy_object_result result;
  ASSERT_TRUE(
      succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                     extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"target"}, info)));
  EXPECT_EQ(info.content_type, options.metadata.content_type);
  EXPECT_EQ(info.content_disposition, options.metadata.content_disposition);
  EXPECT_EQ(info.content_language, options.metadata.content_language);
  ASSERT_EQ(info.custom_metadata.size(), 1u);
  EXPECT_EQ(info.custom_metadata[0].value, "target");
}

TEST_F(ObjectStoreApiTest, CalculatesARequestedCopyChecksumWithoutReplacingMetadata)
{
  extora::object_metadata metadata;
  metadata.content_type = "text/source";
  metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "source"});
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "copy body", metadata)));

  extora::copy_object_options options;
  options.target_checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  extora::copy_object_result result;
  ASSERT_TRUE(
      succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                     extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));
  EXPECT_EQ(result.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(result.checksum.value, apiChecksumValue("copy body"));
  EXPECT_EQ(result.checksum.type, extora::object_checksum_type::full_object);

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"target"}, info)));
  EXPECT_EQ(info.content_type, metadata.content_type);
  ASSERT_EQ(info.custom_metadata.size(), 1u);
  EXPECT_EQ(info.custom_metadata[0].value, "source");
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, result.checksum.checksum_algorithm.value);
  EXPECT_EQ(info.checksum->value, result.checksum.value);
}

TEST_F(ObjectStoreApiTest, RejectsUnsupportedCopyChecksumOptions)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "copy body")));

  extora::copy_object_options options;
  extora::copy_object_result result;
  options.target_checksum_algorithm = extora::checksum_algorithm_name{"unsupported"};
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)
                .code,
            extora::storage_error_code::unsupported_checksum_algorithm);
}

TEST_F(ObjectStoreApiTest, RecalculatesACompositeChecksumWhenCopying)
{
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  createOptions.checksum_type = extora::object_checksum_type::composite;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(
      succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"multipart-source"},
                                                 extora::object_metadata{}, upload, createOptions)));
  VectorReader reader{bytesFromString("copy body"), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"multipart-source"},
                                             upload.upload_id, 1, reader, part)));
  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  extora::put_object_result source;
  ASSERT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"},
                                                           extora::object_key{"multipart-source"}, upload.upload_id,
                                                           completeOptions, source)));
  ASSERT_EQ(source.checksum.type, extora::object_checksum_type::composite);
  extora::object_part_list sourceParts;
  ASSERT_TRUE(succeeded(
      m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"multipart-source"}, sourceParts)));
  EXPECT_EQ(sourceParts.total_parts, 1u);

  extora::copy_object_result target;
  ASSERT_TRUE(succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"multipart-source"},
                                             extora::bucket_name{"photos"}, extora::object_key{"copy"}, target)));
  EXPECT_EQ(target.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(target.checksum.value, apiChecksumValue("copy body"));
  EXPECT_EQ(target.checksum.type, extora::object_checksum_type::full_object);
  extora::object_part_list targetParts;
  ASSERT_TRUE(
      succeeded(m_store->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"copy"}, targetParts)));
  EXPECT_EQ(targetParts.total_parts, 0u);
}

TEST_F(ObjectStoreApiTest, AppliesSourceAndTargetConditionsToCopy)
{
  extora::put_object_result source;
  extora::put_object_result target;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "source", {}, {}, &source)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "target", "target", {}, {}, &target)));

  extora::copy_object_options options;
  options.source_conditions.if_match_etag = "wrong";
  extora::copy_object_result result;
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)
                .code,
            extora::storage_error_code::precondition_failed);

  options.source_conditions.if_match_etag = source.etag;
  options.target_conditions.if_match_etag = target.etag;
  EXPECT_TRUE(
      succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                     extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));
}

TEST_F(ObjectStoreApiTest, CopiesAnExplicitSourceVersion)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::put_object_result first;
  extora::put_object_result second;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "first", {}, {}, &first)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "second", {}, {}, &second)));

  extora::copy_object_result latestResult;
  ASSERT_TRUE(succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                             extora::bucket_name{"photos"}, extora::object_key{"latest-target"},
                                             latestResult)));
  EXPECT_EQ(latestResult.source_version_id.value, second.version_id.value);

  extora::copy_object_options options;
  options.source_version_id = first.version_id;
  extora::copy_object_result result;
  ASSERT_TRUE(
      succeeded(m_store->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                     extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));
  EXPECT_EQ(result.source_version_id.value, first.version_id.value);
  EXPECT_NE(result.modified_at, std::chrono::system_clock::time_point{});
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "target", text)));
  EXPECT_EQ(text, "first");

  extora::head_object_options targetOptions;
  targetOptions.version_id = result.version_id;
  extora::object_info targetInfo;
  ASSERT_TRUE(succeeded(
      m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"target"}, targetInfo, targetOptions)));
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(result.modified_at.time_since_epoch()),
            std::chrono::duration_cast<std::chrono::milliseconds>(targetInfo.modified_at.time_since_epoch()));

  extora::delete_object_result marker;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, marker)));
  options.source_version_id = marker.version_id;
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"marker-copy"}, result, options)
                .code,
            extora::storage_error_code::object_is_delete_marker);

  options.source_version_id = extora::object_version_id{"missing-version"};
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"missing-copy"}, result, options)
                .code,
            extora::storage_error_code::object_version_not_found);
}

TEST_F(ObjectStoreApiTest, ReportsMissingCopyAddresses)
{
  extora::copy_object_result result;
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"missing"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result)
                .code,
            extora::storage_error_code::object_not_found);
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "source", "data")));
  EXPECT_EQ(m_store
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"missing"}, extora::object_key{"target"}, result)
                .code,
            extora::storage_error_code::bucket_not_found);
}

} // namespace extoraTest
