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

#ifndef EXTORA_TEST_OBJECT_INDEX_CONTRACT_H
#define EXTORA_TEST_OBJECT_INDEX_CONTRACT_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <extora/core/object_index.h>

#include <PublicTestSupport.h>

namespace extoraTest {

inline extora::core::indexed_object makeContractObject(const extora::core::physical_extent& extent)
{
  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"object"};
  object.metadata.content_length = extent.length;
  object.metadata.content_type = std::string{"text\0plain", 10};
  object.metadata.custom_metadata.push_back(
      extora::metadata_entry{std::string{"source\0name", 11}, std::string{"contract\0value", 14}});
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "contract-value"};
  object.etag = "contract-etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  object.payload.extents.push_back(extent);
  return object;
}

template<typename TypeParam>
class ObjectIndexContract : public testing::Test {
protected:
  void SetUp() override
  {
    extora::storage_error error;
    m_index = TypeParam::create(makeTempRoot(), error);
    ASSERT_NE(m_index, nullptr);
    ASSERT_TRUE(succeeded(error)) << error.message;
  }

  extora::storage_error reserveExtent(std::uint64_t requestedLength, extora::core::physical_extent& extent)
  {
    ++m_operationSequence;
    return m_index->reserve_extent("contract-operation-" + std::to_string(m_operationSequence), 0, requestedLength,
                                   extent);
  }

  extora::storage_error reclaim(const extora::reclaim_storage_options& options = {})
  {
    extora::core::storage_reclamation_plan plan;
    extora::reclaim_storage_result result;
    extora::storage_error error = m_index->prepare_reclamation({}, options, plan, result);
    if (failed(error)) return error;
    return m_index->finish_reclamation(plan);
  }

  std::unique_ptr<extora::core::object_index> m_index;
  std::uint64_t m_operationSequence = 0;
};

TYPED_TEST_SUITE_P(ObjectIndexContract);

TYPED_TEST_P(ObjectIndexContract, ManagesBucketLifecycle)
{
  const extora::bucket_name bucket{"photos"};
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(bucket)));
  EXPECT_EQ(this->m_index->create_bucket(bucket).code, extora::storage_error_code::bucket_already_exists);

  extora::bucket_info info;
  ASSERT_TRUE(succeeded(this->m_index->find_bucket(bucket, info)));
  EXPECT_EQ(info.name.value, bucket.value);

  extora::bucket_list list;
  ASSERT_TRUE(succeeded(this->m_index->list_buckets(extora::list_buckets_options{}, list)));
  ASSERT_EQ(list.buckets.size(), 1u);
  EXPECT_EQ(list.buckets[0].name.value, bucket.value);

  ASSERT_TRUE(succeeded(this->m_index->delete_bucket(bucket)));
  EXPECT_EQ(this->m_index->find_bucket(bucket, info).code, extora::storage_error_code::bucket_not_found);
}

TYPED_TEST_P(ObjectIndexContract, PublishesFindsListsAndDeletesAnObject)
{
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(extora::bucket_name{"photos"})));
  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, extent)));
  extent.length = 5;

  const extora::core::indexed_object object = makeContractObject(extent);
  ASSERT_TRUE(succeeded(this->m_index->publish_object(object)));

  extora::core::indexed_object found;
  ASSERT_TRUE(succeeded(this->m_index->find_object(object.bucket, object.key, found)));
  EXPECT_EQ(found.etag, object.etag);
  ASSERT_TRUE(found.metadata.content_length.has_value());
  EXPECT_EQ(*found.metadata.content_length, 5u);
  ASSERT_EQ(found.payload.extents.size(), 1u);
  EXPECT_EQ(found.payload.internal_checksum.checksum_algorithm.value,
            object.payload.internal_checksum.checksum_algorithm.value);
  EXPECT_EQ(found.payload.internal_checksum.value, object.payload.internal_checksum.value);
  EXPECT_EQ(found.metadata.content_type, object.metadata.content_type);
  ASSERT_EQ(found.metadata.custom_metadata.size(), 1u);
  EXPECT_EQ(found.metadata.custom_metadata[0].name, object.metadata.custom_metadata[0].name);
  EXPECT_EQ(found.metadata.custom_metadata[0].value, object.metadata.custom_metadata[0].value);

  extora::core::indexed_object_page page;
  ASSERT_TRUE(succeeded(this->m_index->list_objects(object.bucket, extora::list_objects_options{}, page)));
  ASSERT_EQ(page.objects.size(), 1u);
  EXPECT_EQ(page.objects[0].key.value, object.key.value);
  EXPECT_EQ(page.objects[0].metadata.content_type, object.metadata.content_type);
  ASSERT_EQ(page.objects[0].metadata.custom_metadata.size(), 1u);
  EXPECT_EQ(page.objects[0].metadata.custom_metadata[0].name, object.metadata.custom_metadata[0].name);
  EXPECT_EQ(page.objects[0].metadata.custom_metadata[0].value, object.metadata.custom_metadata[0].value);

  extora::delete_object_result deleteResult;
  ASSERT_TRUE(
      succeeded(this->m_index->delete_object(object.bucket, object.key, extora::delete_object_options{},
                                             extora::object_version_id{extora::null_version_id}, deleteResult)));
  EXPECT_EQ(this->m_index->find_object(object.bucket, object.key, found).code,
            extora::storage_error_code::object_not_found);
}

TYPED_TEST_P(ObjectIndexContract, PublishesEmptyObjectWithoutExtents)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"empty"};
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(bucket)));

  extora::core::indexed_object object;
  object.bucket = bucket;
  object.key = key;
  object.metadata.content_length = 0;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "empty-value"};
  object.etag = "empty-etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  ASSERT_TRUE(succeeded(this->m_index->publish_object(object)));

  extora::core::indexed_object found;
  ASSERT_TRUE(succeeded(this->m_index->find_object(bucket, key, found)));
  ASSERT_TRUE(found.metadata.content_length.has_value());
  EXPECT_EQ(*found.metadata.content_length, 0u);
  EXPECT_TRUE(found.payload.extents.empty());
  EXPECT_EQ(found.payload.internal_checksum.value, object.payload.internal_checksum.value);
}

TYPED_TEST_P(ObjectIndexContract, PersistsCompletedObjectPartManifest)
{
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(extora::bucket_name{"photos"})));
  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, extent)));
  extent.length = 5;

  extora::core::indexed_object object = makeContractObject(extent);
  object.parts.push_back(
      extora::object_part_info{1, 0, 2,
                               extora::object_checksum{extora::checksum_algorithm_name{"contract-sum"}, "first",
                                                       extora::object_checksum_type::full_object}});
  object.parts.push_back(
      extora::object_part_info{3, 2, 3,
                               extora::object_checksum{extora::checksum_algorithm_name{"contract-sum"}, "second",
                                                       extora::object_checksum_type::full_object}});
  ASSERT_TRUE(succeeded(this->m_index->publish_object(object)));

  extora::core::indexed_object found;
  ASSERT_TRUE(succeeded(this->m_index->find_object_metadata(object.bucket, object.key, found)));
  extora::core::indexed_object_part_page first;
  ASSERT_TRUE(succeeded(this->m_index->list_object_parts(found, 0, 1, first)));
  EXPECT_EQ(first.total_parts, 2u);
  ASSERT_EQ(first.parts.size(), 1u);
  EXPECT_EQ(first.parts[0].part_number, 1u);
  EXPECT_EQ(first.parts[0].offset, 0u);
  EXPECT_EQ(first.parts[0].content_length, 2u);
  EXPECT_EQ(first.parts[0].checksum.value, "first");
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_part_number_marker.has_value());
  EXPECT_EQ(*first.next_part_number_marker, 1u);

  extora::core::indexed_object_part_page second;
  ASSERT_TRUE(succeeded(this->m_index->list_object_parts(found, *first.next_part_number_marker, 1, second)));
  EXPECT_EQ(second.total_parts, 2u);
  ASSERT_EQ(second.parts.size(), 1u);
  EXPECT_EQ(second.parts[0].part_number, 3u);
  EXPECT_EQ(second.parts[0].offset, 2u);
  EXPECT_EQ(second.parts[0].checksum.value, "second");
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_part_number_marker.has_value());
}

TYPED_TEST_P(ObjectIndexContract, ReusesAbandonedStorageAfterReclamation)
{
  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, extent)));
  const std::vector<extora::core::physical_extent> extents{extent};
  ASSERT_TRUE(succeeded(this->m_index->abandon_extents(extents)));
  extora::core::storage_reclamation_plan plan;
  extora::reclaim_storage_result result;
  ASSERT_TRUE(succeeded(this->m_index->prepare_reclamation({}, {}, plan, result)));
  EXPECT_EQ(result.reclaimed_bytes, 8u);
  EXPECT_EQ(result.reclaimed_extent_count, 1u);
  EXPECT_EQ(result.remaining_reclaimable_bytes, 0u);
  EXPECT_EQ(result.remaining_reclaimable_extent_count, 0u);
  ASSERT_EQ(plan.extents.size(), 1u);
  EXPECT_EQ(plan.extents[0].segment_id, extent.segment_id);
  EXPECT_EQ(plan.extents[0].offset, extent.offset);
  ASSERT_EQ(plan.fully_free_segment_ids.size(), 1u);
  EXPECT_EQ(plan.fully_free_segment_ids[0], extent.segment_id);
  ASSERT_TRUE(succeeded(this->m_index->finish_reclamation(plan)));

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, reused)));
  EXPECT_EQ(reused.segment_id, extent.segment_id);
  EXPECT_EQ(reused.offset, extent.offset);
}

TYPED_TEST_P(ObjectIndexContract, DoesNotReserveAnExtentClaimedForPhysicalRelease)
{
  extora::core::physical_extent releasedExtent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, releasedExtent)));
  ASSERT_TRUE(succeeded(this->m_index->abandon_extents({releasedExtent})));

  extora::core::storage_reclamation_plan plan;
  extora::reclaim_storage_result result;
  ASSERT_TRUE(succeeded(this->m_index->prepare_reclamation({}, {}, plan, result)));
  ASSERT_EQ(plan.extents.size(), 1u);

  extora::core::physical_extent concurrentExtent;
  ASSERT_TRUE(succeeded(this->reserveExtent(4, concurrentExtent)));
  EXPECT_NE(concurrentExtent.offset, releasedExtent.offset);

  ASSERT_TRUE(succeeded(this->m_index->finish_reclamation(plan)));
  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, reused)));
  EXPECT_EQ(reused.segment_id, releasedExtent.segment_id);
  EXPECT_EQ(reused.offset, releasedExtent.offset);
}

TYPED_TEST_P(ObjectIndexContract, PreservesCorruptedPayloadUntilDeletionIsRequested)
{
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(extora::bucket_name{"photos"})));
  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, extent)));
  extent.length = 5;

  const extora::core::indexed_object object = makeContractObject(extent);
  ASSERT_TRUE(succeeded(this->m_index->publish_object(object)));

  extora::core::indexed_object found;
  ASSERT_TRUE(succeeded(this->m_index->find_object(object.bucket, object.key, found)));
  ASSERT_NE(found.payload.id, 0u);
  ASSERT_TRUE(succeeded(this->m_index->mark_payload_corrupted(found.payload.id)));

  ASSERT_TRUE(succeeded(this->m_index->find_object(object.bucket, object.key, found)));
  EXPECT_TRUE(found.is_corrupted);

  ASSERT_TRUE(succeeded(this->reclaim()));
  ASSERT_TRUE(succeeded(this->m_index->find_object(object.bucket, object.key, found)));
  EXPECT_TRUE(found.is_corrupted);

  extora::reclaim_storage_options options;
  options.delete_corrupted_objects = true;
  ASSERT_TRUE(succeeded(this->reclaim(options)));
  EXPECT_EQ(this->m_index->find_object(object.bucket, object.key, found).code,
            extora::storage_error_code::object_not_found);

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(this->reserveExtent(5, reused)));
  EXPECT_EQ(reused.segment_id, extent.segment_id);
  EXPECT_EQ(reused.offset, extent.offset);
}

TYPED_TEST_P(ObjectIndexContract, PersistsAndAbortsMultipartParts)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"object"};
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(bucket)));

  extora::core::indexed_multipart_upload upload;
  upload.upload_id = extora::multipart_upload_id{"upload"};
  upload.bucket = bucket;
  upload.key = key;
  upload.checksum_algorithm = extora::checksum_algorithm_name{"contract-sum"};
  upload.checksum_type = extora::object_checksum_type::composite;
  upload.initiated_at = std::chrono::system_clock::now();
  ASSERT_TRUE(succeeded(this->m_index->create_multipart_upload(upload)));

  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(this->reserveExtent(8, extent)));
  extent.length = 4;
  extora::core::indexed_multipart_part part;
  part.info.part_number = 1;
  part.info.content_length = 4;
  part.info.etag = "part-etag";
  part.info.created_at = std::chrono::system_clock::now();
  part.internal_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "part-value"};
  part.extents.push_back(extent);
  ASSERT_TRUE(succeeded(this->m_index->store_multipart_part(upload.upload_id, bucket, key, part)));

  extora::core::indexed_multipart_upload found;
  ASSERT_TRUE(succeeded(this->m_index->find_multipart_upload(upload.upload_id, found)));
  EXPECT_EQ(found.checksum_algorithm.value, upload.checksum_algorithm.value);
  EXPECT_EQ(found.checksum_type, upload.checksum_type);
  ASSERT_EQ(found.parts.size(), 1u);
  EXPECT_EQ(found.parts[0].info.etag, part.info.etag);
  EXPECT_EQ(found.parts[0].internal_checksum.checksum_algorithm.value, part.internal_checksum.checksum_algorithm.value);
  EXPECT_EQ(found.parts[0].internal_checksum.value, part.internal_checksum.value);

  ASSERT_TRUE(succeeded(this->m_index->abort_multipart_upload(upload.upload_id, bucket, key)));
  EXPECT_EQ(this->m_index->find_multipart_upload(upload.upload_id, found).code,
            extora::storage_error_code::multipart_upload_not_found);
}

TYPED_TEST_P(ObjectIndexContract, ReportsLogicalBucketUsage)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"object"};
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(bucket)));

  extora::core::physical_extent objectExtent;
  ASSERT_TRUE(succeeded(this->reserveExtent(5, objectExtent)));
  ASSERT_TRUE(succeeded(this->m_index->publish_object(makeContractObject(objectExtent))));

  extora::core::indexed_multipart_upload upload;
  upload.upload_id = extora::multipart_upload_id{"upload"};
  upload.bucket = bucket;
  upload.key = key;
  upload.checksum_algorithm = extora::checksum_algorithm_name{"contract-sum"};
  upload.checksum_type = extora::object_checksum_type::composite;
  upload.initiated_at = std::chrono::system_clock::now();
  ASSERT_TRUE(succeeded(this->m_index->create_multipart_upload(upload)));

  extora::core::physical_extent partExtent;
  ASSERT_TRUE(succeeded(this->reserveExtent(4, partExtent)));
  extora::core::indexed_multipart_part part;
  part.info.part_number = 1;
  part.info.content_length = 4;
  part.info.etag = "part-etag";
  part.info.created_at = std::chrono::system_clock::now();
  part.internal_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "part-value"};
  part.extents.push_back(partExtent);
  ASSERT_TRUE(succeeded(this->m_index->store_multipart_part(upload.upload_id, bucket, key, part)));

  extora::bucket_usage usage;
  ASSERT_TRUE(succeeded(this->m_index->get_bucket_usage(bucket, usage)));
  EXPECT_EQ(usage.current_object_bytes, 5u);
  EXPECT_EQ(usage.noncurrent_version_bytes, 0u);
  EXPECT_EQ(usage.multipart_bytes, 4u);
  EXPECT_EQ(usage.current_object_count, 1u);
  EXPECT_EQ(usage.noncurrent_version_count, 0u);
  EXPECT_EQ(usage.multipart_part_count, 1u);

  EXPECT_EQ(this->m_index->get_bucket_usage(extora::bucket_name{"missing"}, usage).code,
            extora::storage_error_code::bucket_not_found);
}

TYPED_TEST_P(ObjectIndexContract, OrdersSameKeyMultipartUploadsByInitiationTime)
{
  const extora::bucket_name bucket{"photos"};
  ASSERT_TRUE(succeeded(this->m_index->create_bucket(bucket)));

  extora::core::indexed_multipart_upload later;
  later.upload_id = extora::multipart_upload_id{"u_00000000000000000000000000000000"};
  later.bucket = bucket;
  later.key = extora::object_key{"object"};
  later.initiated_at = std::chrono::system_clock::time_point{std::chrono::milliseconds{200}};
  ASSERT_TRUE(succeeded(this->m_index->create_multipart_upload(later)));

  extora::core::indexed_multipart_upload earlier = later;
  earlier.upload_id = extora::multipart_upload_id{"u_ffffffffffffffffffffffffffffffff"};
  earlier.initiated_at = std::chrono::system_clock::time_point{std::chrono::milliseconds{100}};
  ASSERT_TRUE(succeeded(this->m_index->create_multipart_upload(earlier)));

  std::vector<extora::core::indexed_multipart_upload> uploads;
  ASSERT_TRUE(succeeded(this->m_index->list_multipart_uploads(bucket, uploads)));
  ASSERT_EQ(uploads.size(), 2u);
  EXPECT_EQ(uploads[0].upload_id.value, earlier.upload_id.value);
  EXPECT_EQ(uploads[1].upload_id.value, later.upload_id.value);
}

REGISTER_TYPED_TEST_SUITE_P(ObjectIndexContract, ManagesBucketLifecycle, PublishesFindsListsAndDeletesAnObject,
                            PublishesEmptyObjectWithoutExtents, PersistsCompletedObjectPartManifest,
                            ReusesAbandonedStorageAfterReclamation, DoesNotReserveAnExtentClaimedForPhysicalRelease,
                            PreservesCorruptedPayloadUntilDeletionIsRequested, PersistsAndAbortsMultipartParts,
                            ReportsLogicalBucketUsage, OrdersSameKeyMultipartUploadsByInitiationTime);

} // namespace extoraTest

#endif // EXTORA_TEST_OBJECT_INDEX_CONTRACT_H
