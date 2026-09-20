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

#include "extora/object_store_factory.h"

#include <cstdint>
#include <limits>
#include <memory>

#include "extora/core/object_store_core.h"
#include "extora/core/segment_data_store.h"
#include "extora/core/sqlite_object_index.h"
#include "extora/core/xxhash_hasher_factory.h"
#include "storage_root_lock.h"

namespace extora {

namespace {

storage_error validate_options(const object_store_options& options)
{
  if (options.root_directory.empty())
    return make_error(storage_error_code::invalid_configuration, "storage root directory is empty");
  if (options.segment_capacity == 0)
    return make_error(storage_error_code::invalid_configuration, "segment capacity is zero");
  if (options.segment_capacity > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return make_error(storage_error_code::invalid_configuration, "segment capacity exceeds filesystem offset range");
  if (options.max_extent_size == 0)
    return make_error(storage_error_code::invalid_configuration, "maximum extent size is zero");

  switch (options.segment_allocation) {
  case segment_allocation_strategy::sparse:
  case segment_allocation_strategy::reserve:
    break;
  default:
    return make_error(storage_error_code::invalid_configuration, "invalid segment allocation strategy");
  }

  switch (options.durability) {
  case storage_durability::strict:
  case storage_durability::balanced:
  case storage_durability::relaxed:
    break;
  default:
    return make_error(storage_error_code::invalid_configuration, "invalid storage durability policy");
  }

  return {};
}

class object_store_impl final : public managed_object_store {
public:
  explicit object_store_impl(const object_store_options& options)
    : m_root_lock{options.root_directory}
    , m_hash_factory{options.custom_hasher_factory}
    , m_index{options.root_directory / "index.sqlite3", options.segment_capacity, options.durability}
    , m_data_store{options.root_directory, options.segment_capacity, options.durability, options.segment_allocation}
    , m_core{m_index, m_data_store, m_hash_factory, make_core_options(options)}
  {}

  storage_error open()
  {
    storage_error error = m_root_lock.acquire();
    if (failed(error)) return error;

    error = m_data_store.open();
    if (failed(error)) return error;

    error = m_index.open();
    if (failed(error)) return error;

    error = m_core.recover();
    if (failed(error)) return error;

    return m_index.checkpoint();
  }

  storage_error create_bucket(const bucket_name& bucket) override
  {
    return m_core.create_bucket(bucket);
  }

  storage_error delete_bucket(const bucket_name& bucket) override
  {
    return m_core.delete_bucket(bucket);
  }

  storage_error head_bucket(const bucket_name& bucket, bucket_info& info) override
  {
    return m_core.head_bucket(bucket, info);
  }

  storage_error list_buckets(bucket_list& result, const list_buckets_options& options) override
  {
    return m_core.list_buckets(result, options);
  }

  storage_error set_bucket_versioning(const bucket_name& bucket, bucket_versioning_configuration configuration) override
  {
    return m_core.set_bucket_versioning(bucket, configuration);
  }

  storage_error get_bucket_versioning(const bucket_name& bucket, bucket_versioning_status& status) override
  {
    return m_core.get_bucket_versioning(bucket, status);
  }

  storage_error put_object(const bucket_name& bucket, const object_key& key, object_reader& reader,
                           const object_metadata& metadata, put_object_result& result,
                           const put_object_options& options) override
  {
    return m_core.put_object(bucket, key, reader, metadata, result, options);
  }

  storage_error create_multipart_upload(const bucket_name& bucket, const object_key& key,
                                        const object_metadata& metadata, create_multipart_upload_result& result,
                                        const create_multipart_upload_options& options) override
  {
    return m_core.create_multipart_upload(bucket, key, metadata, result, options);
  }

  storage_error upload_part(const bucket_name& bucket, const object_key& key, const multipart_upload_id& upload_id,
                            std::uint32_t part_number, object_reader& reader, upload_part_result& result,
                            const upload_part_options& options) override
  {
    return m_core.upload_part(bucket, key, upload_id, part_number, reader, result, options);
  }

  storage_error complete_multipart_upload(const bucket_name& bucket, const object_key& key,
                                          const multipart_upload_id& upload_id,
                                          const complete_multipart_upload_options& options,
                                          put_object_result& result) override
  {
    return m_core.complete_multipart_upload(bucket, key, upload_id, options, result);
  }

  storage_error abort_multipart_upload(const bucket_name& bucket, const object_key& key,
                                       const multipart_upload_id& upload_id) override
  {
    return m_core.abort_multipart_upload(bucket, key, upload_id);
  }

  storage_error list_parts(const bucket_name& bucket, const object_key& key, const multipart_upload_id& upload_id,
                           multipart_part_list& result, const list_parts_options& options) override
  {
    return m_core.list_parts(bucket, key, upload_id, result, options);
  }

  storage_error list_multipart_uploads(const bucket_name& bucket, multipart_upload_list& result,
                                       const list_multipart_uploads_options& options) override
  {
    return m_core.list_multipart_uploads(bucket, result, options);
  }

  storage_error copy_object(const bucket_name& source_bucket, const object_key& source_key,
                            const bucket_name& target_bucket, const object_key& target_key, copy_object_result& result,
                            const copy_object_options& options) override
  {
    return m_core.copy_object(source_bucket, source_key, target_bucket, target_key, result, options);
  }

  storage_error open_object(const bucket_name& bucket, const object_key& key, open_object_result& result,
                            const open_object_options& options) override
  {
    return m_core.open_object(bucket, key, result, options);
  }

  storage_error head_object(const bucket_name& bucket, const object_key& key, object_info& info,
                            const head_object_options& options) override
  {
    return m_core.head_object(bucket, key, info, options);
  }

  storage_error list_object_parts(const bucket_name& bucket, const object_key& key, object_part_list& result,
                                  const list_object_parts_options& options) override
  {
    return m_core.list_object_parts(bucket, key, result, options);
  }

  storage_error delete_object(const bucket_name& bucket, const object_key& key, delete_object_result& result,
                              const delete_object_options& options) override
  {
    return m_core.delete_object(bucket, key, result, options);
  }

  storage_error list_objects(const bucket_name& bucket, object_list& result,
                             const list_objects_options& options) override
  {
    return m_core.list_objects(bucket, result, options);
  }

  storage_error list_object_versions(const bucket_name& bucket, object_version_list& result,
                                     const list_object_versions_options& options) override
  {
    return m_core.list_object_versions(bucket, result, options);
  }

  storage_error get_bucket_usage(const bucket_name& bucket, bucket_usage& usage) override
  {
    return m_core.get_bucket_usage(bucket, usage);
  }

  storage_error get_reclamation_estimate(reclamation_estimate& estimate) override
  {
    return m_core.get_reclamation_estimate(estimate);
  }

  storage_error reclaim_storage(reclaim_storage_result& result, const reclaim_storage_options& options) override
  {
    return m_core.reclaim_storage(result, options);
  }

  storage_error compact_storage(compact_storage_result& result, const compact_storage_options& options) override
  {
    return m_core.compact_storage(result, options);
  }

private:
  static core::object_store_core_options make_core_options(const object_store_options& options)
  {
    core::object_store_core_options core_options;
    core_options.max_extent_size = options.max_extent_size;
    core_options.object_lookup_cache_capacity = options.object_lookup_cache_capacity;
    core_options.max_concurrent_reads = options.max_concurrent_reads;
    core_options.max_concurrent_writes = options.max_concurrent_writes;
    core_options.dedup_min_object_size = options.dedup_min_object_size;
    core_options.multipart_min_part_size = options.multipart_min_part_size;
    core_options.automatic_reclamation_threshold_bytes = options.automatic_reclamation_threshold_bytes;
    core_options.observer = options.observer;
    core_options.operation_progress_interval_bytes = options.operation_progress_interval_bytes;
    return core_options;
  }

  storage_root_lock m_root_lock;
  core::composite_hasher_factory m_hash_factory;
  core::sqlite_object_index m_index;
  core::segment_data_store m_data_store;
  core::object_store_core m_core;
};

} // namespace

std::unique_ptr<managed_object_store> open_object_store(const object_store_options& options, storage_error& error)
{
  error = validate_options(options);
  if (failed(error)) return {};

  std::unique_ptr<object_store_impl> store = std::make_unique<object_store_impl>(options);

  error = store->open();
  if (failed(error)) return {};

  return store;
}

} // namespace extora
