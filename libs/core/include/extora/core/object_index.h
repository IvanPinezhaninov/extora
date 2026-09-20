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

#ifndef EXTORA_CORE_OBJECT_INDEX_H
#define EXTORA_CORE_OBJECT_INDEX_H

#include <cstdint>
#include <string_view>
#include <vector>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

namespace extora::core {

// Stores names and metadata, but not object bytes. The coordinator may invoke
// public read-path calls concurrently. Recovery and mutation calls are exclusive.
class object_index {
public:
  virtual ~object_index() = default;

  virtual storage_error create_bucket(const bucket_name& bucket) = 0;

  virtual storage_error delete_bucket(const bucket_name& bucket) = 0;

  virtual storage_error find_bucket(const bucket_name& bucket, bucket_info& result) = 0;

  virtual storage_error list_buckets(const list_buckets_options& options, bucket_list& result) = 0;

  virtual storage_error get_bucket_usage(const bucket_name& bucket, bucket_usage& usage) = 0;

  virtual storage_error set_bucket_versioning(const bucket_name& bucket, bucket_versioning_status status) = 0;

  virtual storage_error reserve_extent(std::string_view operation_id, std::uint64_t operation_ordinal,
                                       std::uint64_t requested_length, physical_extent& extent,
                                       extent_allocation_mode mode = extent_allocation_mode::reuse) = 0;
  virtual storage_error reserve_extent_at(std::string_view operation_id, std::uint64_t operation_ordinal,
                                          const physical_extent& requested_extent) = 0;
  virtual storage_error abandon_extents(const std::vector<physical_extent>& extents) = 0;

  // Publishes a complete generation atomically.
  virtual storage_error publish_object(const indexed_object& object,
                                       const object_conditions& conditions = object_conditions{}) = 0;

  virtual storage_error find_dedup_object(const checksum_algorithm_name& checksum_algorithm, std::string_view value,
                                          std::uint64_t content_length, indexed_object& result) = 0;

  virtual storage_error create_multipart_upload(const indexed_multipart_upload& upload) = 0;

  virtual storage_error find_multipart_upload(const multipart_upload_id& upload_id,
                                              indexed_multipart_upload& result) = 0;

  virtual storage_error store_multipart_part(const multipart_upload_id& upload_id, const bucket_name& bucket,
                                             const object_key& key, const indexed_multipart_part& part) = 0;

  virtual storage_error complete_multipart_upload(const multipart_upload_id& upload_id, const indexed_object& object,
                                                  const object_conditions& conditions) = 0;

  virtual storage_error abort_multipart_upload(const multipart_upload_id& upload_id, const bucket_name& bucket,
                                               const object_key& key) = 0;

  virtual storage_error list_multipart_uploads(const bucket_name& bucket,
                                               std::vector<indexed_multipart_upload>& result) = 0;

  virtual storage_error find_object(const bucket_name& bucket, const object_key& key, indexed_object& result) = 0;

  virtual storage_error find_object_version(const bucket_name& bucket, const object_key& key,
                                            const object_version_id& version_id, indexed_object& result) = 0;

  virtual storage_error find_object_metadata(const bucket_name& bucket, const object_key& key,
                                             indexed_object& result) = 0;

  virtual storage_error list_object_parts(const indexed_object& object, std::uint32_t part_number_marker,
                                          std::size_t max_parts, indexed_object_part_page& result) = 0;

  virtual storage_error delete_object(const bucket_name& bucket, const object_key& key,
                                      const delete_object_options& options, const object_version_id& marker_version_id,
                                      delete_object_result& result) = 0;

  virtual storage_error list_objects(const bucket_name& bucket, const list_objects_options& options,
                                     indexed_object_page& result) = 0;

  virtual storage_error list_object_versions(const bucket_name& bucket, const list_object_versions_options& options,
                                             indexed_object_version_page& result) = 0;

  // Returns every data-bearing generation for recovery checks.
  virtual storage_error list_object_generations_for_recovery(std::vector<indexed_object>& result) = 0;

  // Quarantines every object generation that references the payload.
  virtual storage_error mark_payload_corrupted(std::uint64_t payload_id) = 0;

  // Atomically replaces one healthy payload's current extent manifest. The
  // replacement may retain ranges from the current manifest.
  virtual storage_error replace_payload_extents(std::uint64_t payload_id,
                                                const std::vector<physical_extent>& expected_extents,
                                                const std::vector<physical_extent>& replacement_extents,
                                                bool& replaced) = 0;

  // Materializes the current append tail as reusable storage and returns a
  // layout snapshot for planning an exact compaction pass.
  virtual storage_error prepare_compaction(storage_compaction_layout& layout) = 0;

  virtual storage_error get_reclamation_estimate(reclamation_estimate& estimate) = 0;

  // Claims free ranges so that the coordinator may release their physical
  // storage without a concurrent reservation reusing the same bytes.
  virtual storage_error prepare_reclamation(const std::vector<physical_extent>& protected_extents,
                                            const reclaim_storage_options& options, storage_reclamation_plan& plan,
                                            reclaim_storage_result& result) = 0;

  // Makes every range claimed by prepare_reclamation reusable again.
  virtual storage_error finish_reclamation(const storage_reclamation_plan& plan) = 0;
};

} // namespace extora::core

#endif // EXTORA_CORE_OBJECT_INDEX_H
