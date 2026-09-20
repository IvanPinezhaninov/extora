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

#include "extora/core/object_store_core.h"

#include <chrono>
#include <utility>

#include "extora/core/object_data_store.h"
#include "extora/core/object_index.h"
#include "extora/core/opaque_id.h"
#include "object_store_core_helpers.h"
#include "operation_tracker.h"
#include "payload_write.h"

namespace extora::core {

using object_store_detail::make_write_version_id;
using object_store_detail::validate_bucket_name;
using object_store_detail::validate_object_address;

namespace object_store_detail {

storage_error check_read_conditions(const indexed_object& object, const object_conditions& conditions)
{
  if (conditions.if_match_etag.has_value() && *conditions.if_match_etag != object.etag)
    return {storage_error_code::precondition_failed, "If-Match condition failed"};

  if (!conditions.if_match_etag.has_value() && conditions.if_unmodified_since.has_value() &&
      object.modified_at > *conditions.if_unmodified_since)
    return {storage_error_code::precondition_failed, "If-Unmodified-Since condition failed"};

  if (conditions.if_none_match_etag.has_value() &&
      (*conditions.if_none_match_etag == etag_wildcard || *conditions.if_none_match_etag == object.etag))
    return {storage_error_code::not_modified, "If-None-Match condition failed"};

  if (conditions.if_modified_since.has_value() && object.modified_at <= *conditions.if_modified_since)
    return {storage_error_code::not_modified, "If-Modified-Since condition failed"};

  return {};
}

storage_error check_write_conditions(const indexed_object& object, const object_conditions& conditions)
{
  storage_error error = check_read_conditions(object, conditions);
  if (error.code == storage_error_code::not_modified) error.code = storage_error_code::precondition_failed;
  return error;
}

object_version_id make_write_version_id(bucket_versioning_status versioning)
{
  if (versioning != bucket_versioning_status::enabled) return object_version_id{null_version_id};

  return object_version_id{generate_opaque_id(object_version_id_prefix)};
}

object_version_id make_delete_marker_version_id(bucket_versioning_status versioning)
{
  if (versioning == bucket_versioning_status::suspended) return object_version_id{null_version_id};

  return object_version_id{generate_opaque_id(delete_marker_version_id_prefix)};
}

} // namespace object_store_detail

namespace {

indexed_object make_indexed_object(const bucket_name& bucket, const object_key& key,
                                   const object_metadata& requested_metadata, bucket_versioning_status versioning,
                                   std::uint64_t dedup_min_object_size, payload_write_result write_result)
{
  indexed_object object;
  object.bucket = bucket;
  object.key = key;
  object.metadata = object_store_detail::make_stored_object_metadata(requested_metadata);
  object.metadata.content_length = write_result.content_length;
  object.metadata.checksum = std::move(write_result.checksum);
  object.etag = std::move(write_result.etag);
  object.version_id = make_write_version_id(versioning);
  object.payload.extents = std::move(write_result.extents);
  object.payload.internal_checksum = std::move(write_result.internal_checksum);
  if (write_result.deduplicate && write_result.content_length >= dedup_min_object_size) {
    object.payload.dedup_algorithm = object.payload.internal_checksum.checksum_algorithm;
    object.payload.dedup_value = object.payload.internal_checksum.value;
  }
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  return object;
}

} // namespace

namespace object_store_detail {

storage_error validate_bucket_name(const bucket_name& bucket)
{
  if (bucket.value.empty()) return make_error(storage_error_code::invalid_bucket_name, "bucket name is empty");
  if (bucket.value.size() > max_bucket_name_size)
    return make_error(storage_error_code::invalid_bucket_name, "bucket name is too long");
  if (bucket.value.find('\0') != std::string::npos)
    return make_error(storage_error_code::invalid_bucket_name, "bucket name contains a null byte");

  return {};
}

storage_error validate_object_key(const object_key& key)
{
  if (key.value.empty()) return make_error(storage_error_code::invalid_object_key, "object key is empty");
  if (key.value.size() > max_object_key_size)
    return make_error(storage_error_code::invalid_object_key, "object key is too long");
  if (key.value.find('\0') != std::string::npos)
    return make_error(storage_error_code::invalid_object_key, "object key contains a null byte");

  return {};
}

storage_error validate_object_address(const bucket_name& bucket, const object_key& key)
{
  const storage_error bucket_validation_error = validate_bucket_name(bucket);
  if (failed(bucket_validation_error)) return bucket_validation_error;

  const storage_error key_validation_error = validate_object_key(key);
  if (failed(key_validation_error)) return key_validation_error;

  return {};
}

std::uint64_t object_content_length(const indexed_object& object)
{
  if (object.metadata.content_length.has_value()) return *object.metadata.content_length;

  std::uint64_t length = 0;
  for (const physical_extent& extent : object.payload.extents)
    length += extent.length;

  return length;
}

object_info to_object_info(const indexed_object& object)
{
  object_info info;
  info.key = object.key;
  info.version_id = object.version_id;
  info.is_delete_marker = object.is_delete_marker;
  info.is_latest = object.is_latest;
  info.etag = object.etag;
  info.content_length = object_content_length(object);
  info.content_type = object.metadata.content_type;
  info.cache_control = object.metadata.cache_control;
  info.content_disposition = object.metadata.content_disposition;
  info.content_encoding = object.metadata.content_encoding;
  info.content_language = object.metadata.content_language;
  info.expires_at = object.metadata.expires_at;
  info.checksum = object.metadata.checksum;
  info.created_at = object.created_at;
  info.modified_at = object.modified_at;
  info.custom_metadata = object.metadata.custom_metadata;
  return info;
}

stored_object_metadata make_stored_object_metadata(const object_metadata& metadata)
{
  stored_object_metadata stored;
  stored.content_type = metadata.content_type;
  stored.cache_control = metadata.cache_control;
  stored.content_disposition = metadata.content_disposition;
  stored.content_encoding = metadata.content_encoding;
  stored.content_language = metadata.content_language;
  stored.expires_at = metadata.expires_at;
  stored.custom_metadata = metadata.custom_metadata;
  return stored;
}

} // namespace object_store_detail

object_store_core::object_store_core(object_index& index, object_data_store& data_store, hasher_factory& hash_factory,
                                     object_store_core_options options)
  : m_index{index}
  , m_data_store{data_store}
  , m_hash_factory{hash_factory}
  , m_max_extent_size{options.max_extent_size == 0 ? default_max_extent_size : options.max_extent_size}
  , m_object_cache{options.object_lookup_cache_capacity}
  , m_read_limiter{options.max_concurrent_reads}
  , m_write_limiter{options.max_concurrent_writes}
  , m_dedup_min_object_size{options.dedup_min_object_size}
  , m_multipart_min_part_size{options.multipart_min_part_size}
  , m_automatic_reclamation_threshold_bytes{options.automatic_reclamation_threshold_bytes}
  , m_observer{options.observer}
  , m_operation_progress_interval_bytes{options.operation_progress_interval_bytes}
{}

storage_error object_store_core::create_bucket(const bucket_name& bucket)
{
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.create_bucket(bucket);
}

storage_error object_store_core::delete_bucket(const bucket_name& bucket)
{
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.delete_bucket(bucket);
}

storage_error object_store_core::head_bucket(const bucket_name& bucket, bucket_info& info)
{
  info = {};
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;

  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.find_bucket(bucket, info);
}

storage_error object_store_core::list_buckets(bucket_list& result, const list_buckets_options& options)
{
  result = {};
  if (options.max_buckets == 0 || options.max_buckets > max_bucket_list_page_size)
    return make_error(storage_error_code::invalid_list_options, "max_buckets must be between 1 and 10000");

  list_buckets_options index_options = options;
  if (!options.continuation_token.empty() &&
      !decode_listing_token(options.continuation_token, bucket_listing_token_prefix, index_options.continuation_token))
    return make_error(storage_error_code::invalid_continuation_token, "invalid bucket continuation token");

  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  const storage_error error = m_index.list_buckets(index_options, result);
  if (failed(error)) return error;
  if (result.next_continuation_token.has_value())
    result.next_continuation_token = encode_listing_token(bucket_listing_token_prefix, *result.next_continuation_token);
  return {};
}

storage_error object_store_core::set_bucket_versioning(const bucket_name& bucket,
                                                       bucket_versioning_configuration configuration)
{
  const storage_error error = validate_bucket_name(bucket);
  if (failed(error)) return error;
  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  const bucket_versioning_status status = configuration == bucket_versioning_configuration::enabled
                                              ? bucket_versioning_status::enabled
                                              : bucket_versioning_status::suspended;
  return m_index.set_bucket_versioning(bucket, status);
}

storage_error object_store_core::get_bucket_versioning(const bucket_name& bucket, bucket_versioning_status& status)
{
  status = bucket_versioning_status::unversioned;
  const storage_error error = validate_bucket_name(bucket);
  if (failed(error)) return error;
  bucket_info info;
  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  const storage_error find_error = m_index.find_bucket(bucket, info);
  if (failed(find_error)) return find_error;
  status = info.versioning;
  return {};
}

storage_error object_store_core::publish_written_object(indexed_object& object, const object_conditions& conditions)
{
  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  bool owns_written_extents = true;

  if (!object.payload.dedup_algorithm.value.empty()) {
    indexed_object duplicate;
    storage_error error = m_index.find_dedup_object(object.payload.dedup_algorithm, object.payload.dedup_value,
                                                    *object.metadata.content_length, duplicate);
    if (!failed(error)) {
      error = m_index.abandon_extents(object.payload.extents);
      if (failed(error)) return error;
      object.payload = std::move(duplicate.payload);
      owns_written_extents = false;
    } else if (error.code != storage_error_code::object_not_found) {
      static_cast<void>(m_index.abandon_extents(object.payload.extents));
      return error;
    }
  }

  const storage_error error = m_index.publish_object(object, conditions);
  if (failed(error)) {
    if (owns_written_extents) static_cast<void>(m_index.abandon_extents(object.payload.extents));
    return error;
  }

  m_object_cache.erase(object.bucket, object.key);
  return {};
}

storage_error object_store_core::put_object(const bucket_name& bucket, const object_key& key, object_reader& reader,
                                            const object_metadata& metadata, put_object_result& result,
                                            const put_object_options& options)
{
  operation_tracker operation{
      m_observer.get(), m_next_operation_id, m_operation_progress_interval_bytes, operation_type::put_object,
      bucket.value,     key.value,           options.expected_content_length};
  const storage_error error = put_object_with_tracker(bucket, key, reader, metadata, options, result, operation);
  operation.finish(error);
  return error;
}

storage_error object_store_core::put_object_with_tracker(const bucket_name& bucket, const object_key& key,
                                                         object_reader& reader, const object_metadata& metadata,
                                                         const put_object_options& options, put_object_result& result,
                                                         operation_tracker& operation)
{
  result = {};
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) return address_validation_error;

  operation_limiter::permit write_permit = m_write_limiter.try_acquire();
  if (!write_permit.acquired()) {
    return make_error(storage_error_code::concurrency_limit_exceeded, "maximum concurrent writes reached");
  }

  bucket_info bucket_state;
  {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.find_bucket(bucket, bucket_state);
    if (failed(error)) return error;
  }

  const bool dedup_size_eligible =
      !options.expected_content_length.has_value() || *options.expected_content_length >= m_dedup_min_object_size;
  const bool use_dedup = options.dedup == dedup_mode::enabled && dedup_size_eligible;

  payload_write_options write_options;
  write_options.expected_content_length = options.expected_content_length;
  write_options.expected_checksum = options.expected_checksum;
  write_options.deduplicate = use_dedup;
  write_options.operation = &operation;

  payload_write_result write_result;
  storage_error error = write_payload(reader, std::move(write_options), write_result);
  if (failed(error)) return error;

  indexed_object object = make_indexed_object(bucket, key, metadata, bucket_state.versioning, m_dedup_min_object_size,
                                              std::move(write_result));
  error = publish_written_object(object, options.conditions);
  if (failed(error)) return error;

  result.etag = object.etag;
  result.version_id = object.version_id;
  result.checksum = *object.metadata.checksum;
  return {};
}

} // namespace extora::core
