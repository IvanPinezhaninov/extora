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

#ifndef EXTORA_CORE_OBJECT_STORE_CORE_H
#define EXTORA_CORE_OBJECT_STORE_CORE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include <extora/core/active_object_registry.h>
#include <extora/core/object_lookup_cache.h>
#include <extora/core/operation_limiter.h>
#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/object_store.h>
#include <extora/object_store_options.h>

namespace extora::core {

class object_data_store;
class object_index;
class operation_tracker;
struct payload_write_options;
struct payload_write_result;

struct object_store_core_options {
  std::uint64_t max_extent_size = default_max_extent_size;
  std::size_t object_lookup_cache_capacity = default_object_lookup_cache_capacity;
  std::optional<std::size_t> max_concurrent_reads;
  std::optional<std::size_t> max_concurrent_writes;
  std::uint64_t dedup_min_object_size = default_dedup_min_object_size;
  std::uint64_t multipart_min_part_size = default_multipart_min_part_size;
  std::optional<std::uint64_t> automatic_reclamation_threshold_bytes;
  std::shared_ptr<operation_observer> observer;
  std::uint64_t operation_progress_interval_bytes = default_operation_progress_interval_bytes;
};

// Thread-safe store over one index and data backend. Reads may run concurrently.
// Mutations are serialized.
class object_store_core final : public object_store {
public:
  object_store_core(object_index& index, object_data_store& data_store, hasher_factory& hash_factory,
                    object_store_core_options options = object_store_core_options{});

  storage_error recover();

  storage_error create_bucket(const bucket_name& bucket) override;

  storage_error delete_bucket(const bucket_name& bucket) override;

  storage_error head_bucket(const bucket_name& bucket, bucket_info& info) override;

  storage_error list_buckets(bucket_list& result, const list_buckets_options& options = {}) override;

  storage_error set_bucket_versioning(const bucket_name& bucket,
                                      bucket_versioning_configuration configuration) override;

  storage_error get_bucket_versioning(const bucket_name& bucket, bucket_versioning_status& status) override;

  storage_error put_object(const bucket_name& bucket, const object_key& key, object_reader& reader,
                           const object_metadata& metadata, put_object_result& result,
                           const put_object_options& options = {}) override;

  storage_error create_multipart_upload(const bucket_name& bucket, const object_key& key,
                                        const object_metadata& metadata, create_multipart_upload_result& result,
                                        const create_multipart_upload_options& options = {}) override;

  storage_error upload_part(const bucket_name& bucket, const object_key& key, const multipart_upload_id& upload_id,
                            std::uint32_t part_number, object_reader& reader, upload_part_result& result,
                            const upload_part_options& options = {}) override;

  storage_error complete_multipart_upload(const bucket_name& bucket, const object_key& key,
                                          const multipart_upload_id& upload_id,
                                          const complete_multipart_upload_options& options,
                                          put_object_result& result) override;

  storage_error abort_multipart_upload(const bucket_name& bucket, const object_key& key,
                                       const multipart_upload_id& upload_id) override;

  storage_error list_parts(const bucket_name& bucket, const object_key& key, const multipart_upload_id& upload_id,
                           multipart_part_list& result, const list_parts_options& options = {}) override;

  storage_error list_multipart_uploads(const bucket_name& bucket, multipart_upload_list& result,
                                       const list_multipart_uploads_options& options = {}) override;

  storage_error copy_object(const bucket_name& source_bucket, const object_key& source_key,
                            const bucket_name& target_bucket, const object_key& target_key, copy_object_result& result,
                            const copy_object_options& options = {}) override;

  storage_error open_object(const bucket_name& bucket, const object_key& key, open_object_result& result,
                            const open_object_options& options = {}) override;

  storage_error head_object(const bucket_name& bucket, const object_key& key, object_info& info,
                            const head_object_options& options = {}) override;

  storage_error list_object_parts(const bucket_name& bucket, const object_key& key, object_part_list& result,
                                  const list_object_parts_options& options = {}) override;

  storage_error delete_object(const bucket_name& bucket, const object_key& key, delete_object_result& result,
                              const delete_object_options& options = {}) override;

  storage_error list_objects(const bucket_name& bucket, object_list& result,
                             const list_objects_options& options = {}) override;

  storage_error list_object_versions(const bucket_name& bucket, object_version_list& result,
                                     const list_object_versions_options& options = {}) override;

  storage_error get_bucket_usage(const bucket_name& bucket, bucket_usage& usage);
  storage_error get_reclamation_estimate(reclamation_estimate& estimate);
  storage_error reclaim_storage(reclaim_storage_result& result, const reclaim_storage_options& options = {});
  storage_error compact_storage(compact_storage_result& result, const compact_storage_options& options = {});

private:
  storage_error open_object_with_tracker(const bucket_name& bucket, const object_key& key,
                                         const open_object_options& options, open_object_result& result,
                                         operation_tracker operation);

  storage_error put_object_with_tracker(const bucket_name& bucket, const object_key& key, object_reader& reader,
                                        const object_metadata& metadata, const put_object_options& options,
                                        put_object_result& result, operation_tracker& operation);

  storage_error upload_part_with_tracker(const bucket_name& bucket, const object_key& key,
                                         const multipart_upload_id& upload_id, std::uint32_t part_number,
                                         object_reader& reader, const upload_part_options& options,
                                         upload_part_result& result, operation_tracker& operation);

  storage_error write_payload(object_reader& reader, payload_write_options options, payload_write_result& result);

  storage_error publish_written_object(indexed_object& object, const object_conditions& conditions);

  storage_error reclaim_storage_if_needed();

  storage_error compact_reclaimed_storage(const compact_storage_options& options, compact_storage_result& result);

  storage_error hash_payload_extents(const std::vector<physical_extent>& extents,
                                     const checksum_algorithm_name& checksum_algorithm,
                                     storage_error_code unavailable_code, const char* unavailable_message,
                                     std::string& value);

  storage_error verify_payload_integrity(const std::vector<physical_extent>& extents, const object_checksum& checksum);

  object_index& m_index;
  object_data_store& m_data_store;
  hasher_factory& m_hash_factory;
  std::uint64_t m_max_extent_size = default_max_extent_size;
  object_lookup_cache m_object_cache;
  operation_limiter m_read_limiter;
  operation_limiter m_write_limiter;
  std::uint64_t m_dedup_min_object_size = default_dedup_min_object_size;
  std::uint64_t m_multipart_min_part_size = default_multipart_min_part_size;
  std::optional<std::uint64_t> m_automatic_reclamation_threshold_bytes;
  std::shared_ptr<operation_observer> m_observer;
  std::uint64_t m_operation_progress_interval_bytes = default_operation_progress_interval_bytes;
  std::atomic<std::uint64_t> m_next_operation_id{1};
  mutable std::shared_mutex m_index_mutex;
  std::mutex m_reclamation_mutex;
  std::mutex m_compaction_mutex;
  active_object_registry m_active_objects;
};

} // namespace extora::core

#endif // EXTORA_CORE_OBJECT_STORE_CORE_H
