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

#include <string>
#include <string_view>

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, CreatesHeadsListsAndDeletesBuckets)
{
  ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"zeta"})));
  ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"alpha"})));
  EXPECT_EQ(m_store->create_bucket(extora::bucket_name{"alpha"}).code,
            extora::storage_error_code::bucket_already_exists);
  extora::bucket_info alpha;
  ASSERT_TRUE(succeeded(m_store->head_bucket(extora::bucket_name{"alpha"}, alpha)));
  EXPECT_EQ(alpha.name.value, "alpha");
  EXPECT_NE(alpha.created_at, std::chrono::system_clock::time_point{});
  EXPECT_EQ(alpha.versioning, extora::bucket_versioning_status::unversioned);

  extora::bucket_list buckets;
  ASSERT_TRUE(succeeded(m_store->list_buckets(buckets)));
  ASSERT_EQ(buckets.buckets.size(), 3u);
  EXPECT_EQ(buckets.buckets[0].name.value, "alpha");
  EXPECT_EQ(buckets.buckets[1].name.value, "photos");
  EXPECT_EQ(buckets.buckets[2].name.value, "zeta");
  EXPECT_EQ(buckets.buckets[0].created_at, alpha.created_at);

  ASSERT_TRUE(succeeded(m_store->delete_bucket(extora::bucket_name{"alpha"})));
  EXPECT_EQ(m_store->head_bucket(extora::bucket_name{"alpha"}, alpha).code,
            extora::storage_error_code::bucket_not_found);
}

TEST_F(ObjectStoreApiTest, PaginatesAndFiltersBucketsWithOpaqueTokens)
{
  for (std::string_view name : {"alpha", "archive", "zeta"})
    ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{std::string{name}})));

  extora::list_buckets_options options;
  options.prefix = "a";
  options.max_buckets = 1;
  extora::bucket_list first;
  ASSERT_TRUE(succeeded(m_store->list_buckets(first, options)));
  ASSERT_EQ(first.buckets.size(), 1u);
  EXPECT_EQ(first.buckets[0].name.value, "alpha");
  ASSERT_TRUE(first.next_continuation_token.has_value());
  EXPECT_NE(*first.next_continuation_token, "alpha");

  options.continuation_token = *first.next_continuation_token;
  extora::bucket_list second;
  ASSERT_TRUE(succeeded(m_store->list_buckets(second, options)));
  ASSERT_EQ(second.buckets.size(), 1u);
  EXPECT_EQ(second.buckets[0].name.value, "archive");
  EXPECT_FALSE(second.next_continuation_token.has_value());
}

TEST_F(ObjectStoreApiTest, RejectsInvalidBucketPaginationOptions)
{
  extora::list_buckets_options options;
  options.max_buckets = 0;
  extora::bucket_list result;
  EXPECT_EQ(m_store->list_buckets(result, options).code, extora::storage_error_code::invalid_list_options);

  options.max_buckets = extora::max_bucket_list_page_size + 1;
  EXPECT_EQ(m_store->list_buckets(result, options).code, extora::storage_error_code::invalid_list_options);

  options.max_buckets = extora::max_bucket_list_page_size;
  options.continuation_token = "not-a-token";
  EXPECT_EQ(m_store->list_buckets(result, options).code, extora::storage_error_code::invalid_continuation_token);
}

TEST_F(ObjectStoreApiTest, RejectsDeletingANonEmptyBucket)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "data")));
  EXPECT_EQ(m_store->delete_bucket(extora::bucket_name{"photos"}).code, extora::storage_error_code::bucket_not_empty);
  extora::bucket_info info;
  EXPECT_TRUE(succeeded(m_store->head_bucket(extora::bucket_name{"photos"}, info)));
}

TEST_F(ObjectStoreApiTest, ValidatesBucketNamesAndMissingBuckets)
{
  EXPECT_EQ(m_store->create_bucket(extora::bucket_name{}).code, extora::storage_error_code::invalid_bucket_name);
  EXPECT_EQ(m_store->create_bucket(extora::bucket_name{std::string(extora::max_bucket_name_size + 1, 'b')}).code,
            extora::storage_error_code::invalid_bucket_name);
  EXPECT_EQ(m_store->create_bucket(extora::bucket_name{std::string{"invalid\0bucket", 14}}).code,
            extora::storage_error_code::invalid_bucket_name);
  extora::bucket_info info;
  info.name.value = "stale";
  EXPECT_EQ(m_store->head_bucket(extora::bucket_name{"missing"}, info).code,
            extora::storage_error_code::bucket_not_found);
  EXPECT_TRUE(info.name.value.empty());

  extora::bucket_versioning_status status = extora::bucket_versioning_status::enabled;
  EXPECT_EQ(m_store->get_bucket_versioning(extora::bucket_name{}, status).code,
            extora::storage_error_code::invalid_bucket_name);
  EXPECT_EQ(status, extora::bucket_versioning_status::unversioned);
  EXPECT_EQ(m_store->delete_bucket(extora::bucket_name{"missing"}).code, extora::storage_error_code::bucket_not_found);
}

TEST_F(ObjectStoreApiTest, ReportsLogicalBucketUsageAcrossVersionsAndMultipartUploads)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "first")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "second")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "keep", "abc")));

  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
  VectorReader partReader{bytesFromString("part"), 2};
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, partReader, ignoredUploadPartResult())));

  extora::bucket_usage usage;
  ASSERT_TRUE(succeeded(m_store->get_bucket_usage(extora::bucket_name{"photos"}, usage)));
  EXPECT_EQ(usage.current_object_bytes, 9u);
  EXPECT_EQ(usage.noncurrent_version_bytes, 5u);
  EXPECT_EQ(usage.multipart_bytes, 4u);
  EXPECT_EQ(usage.current_object_count, 2u);
  EXPECT_EQ(usage.noncurrent_version_count, 1u);
  EXPECT_EQ(usage.multipart_part_count, 1u);

  extora::delete_object_result deleteResult;
  ASSERT_TRUE(
      succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, deleteResult)));
  ASSERT_TRUE(deleteResult.is_delete_marker);
  ASSERT_TRUE(succeeded(m_store->get_bucket_usage(extora::bucket_name{"photos"}, usage)));
  EXPECT_EQ(usage.current_object_bytes, 3u);
  EXPECT_EQ(usage.noncurrent_version_bytes, 11u);
  EXPECT_EQ(usage.current_object_count, 1u);
  EXPECT_EQ(usage.noncurrent_version_count, 2u);

  ASSERT_TRUE(succeeded(
      m_store->abort_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id)));
  ASSERT_TRUE(succeeded(m_store->get_bucket_usage(extora::bucket_name{"photos"}, usage)));
  EXPECT_EQ(usage.multipart_bytes, 0u);
  EXPECT_EQ(usage.multipart_part_count, 0u);

  EXPECT_EQ(m_store->get_bucket_usage(extora::bucket_name{"missing"}, usage).code,
            extora::storage_error_code::bucket_not_found);
  EXPECT_EQ(usage.current_object_bytes, 0u);
  EXPECT_EQ(usage.noncurrent_version_bytes, 0u);
  EXPECT_EQ(usage.multipart_bytes, 0u);
}

} // namespace extoraTest
