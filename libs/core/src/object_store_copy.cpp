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

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "object_store_core_helpers.h"
#include "operation_tracker.h"

namespace extora::core {

using object_store_detail::check_write_conditions;
using object_store_detail::make_write_version_id;
using object_store_detail::validate_object_address;

storage_error object_store_core::copy_object(const bucket_name& source_bucket, const object_key& source_key,
                                             const bucket_name& target_bucket, const object_key& target_key,
                                             copy_object_result& result, const copy_object_options& options)
{
  result = {};
  storage_error error = validate_object_address(source_bucket, source_key);
  if (failed(error)) return error;
  error = validate_object_address(target_bucket, target_key);
  if (failed(error)) return error;

  stored_object_metadata replacement_metadata = object_store_detail::make_stored_object_metadata(options.metadata);
  std::optional<std::string> validated_source_etag;
  std::optional<object_checksum> recalculated_checksum;
  indexed_object source_snapshot;
  {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    error = options.source_version_id.has_value()
                ? m_index.find_object_version(source_bucket, source_key, *options.source_version_id, source_snapshot)
                : m_index.find_object(source_bucket, source_key, source_snapshot);
    if (options.source_version_id.has_value() && error.code == storage_error_code::object_not_found)
      error = make_error(storage_error_code::object_version_not_found, "source object version was not found");
    if (failed(error)) return error;
    if (source_snapshot.is_corrupted)
      return make_error(storage_error_code::object_corrupted, "source object payload is corrupted");
    if (source_snapshot.is_delete_marker)
      return make_error(storage_error_code::object_is_delete_marker, "source object version is a delete marker");
  }

  const bool source_checksum_is_composite = source_snapshot.metadata.checksum.has_value() &&
                                            source_snapshot.metadata.checksum->type == object_checksum_type::composite;
  checksum_algorithm_name requested_checksum_algorithm = options.target_checksum_algorithm;
  if (requested_checksum_algorithm.value.empty() && source_checksum_is_composite) {
    requested_checksum_algorithm = source_snapshot.metadata.checksum->checksum_algorithm;
  }

  if (!requested_checksum_algorithm.value.empty()) {
    storage_error hasher_error;
    std::unique_ptr<hasher> checksum_hasher = m_hash_factory.create_hasher(requested_checksum_algorithm, hasher_error);
    if (failed(hasher_error)) return hasher_error;
    if (!checksum_hasher)
      return make_error(storage_error_code::unsupported_checksum_algorithm, "unsupported object checksum algorithm");

    error = check_write_conditions(source_snapshot, options.source_conditions);
    if (error.code == storage_error_code::not_modified) return {storage_error_code::precondition_failed, error.message};
    if (failed(error)) return error;
    validated_source_etag = source_snapshot.etag;

    open_object_options open_options;
    open_options.version_id = options.source_version_id;
    open_options.conditions.if_match_etag = *validated_source_etag;
    open_options.verify_integrity = true;
    open_object_result open_result;
    operation_tracker read_operation{nullptr,
                                     m_next_operation_id,
                                     m_operation_progress_interval_bytes,
                                     operation_type::open_object,
                                     source_bucket.value,
                                     source_key.value};
    error = open_object_with_tracker(source_bucket, source_key, open_options, open_result, std::move(read_operation));
    if (failed(error)) return error;

    std::array<std::byte, 64 * 1024> buffer;
    while (true) {
      const object_read_result read_result = open_result.reader->read(buffer.data(), buffer.size());
      if (failed(read_result.error)) return read_result.error;
      if (read_result.bytes_read > 0) {
        error = checksum_hasher->update(buffer.data(), read_result.bytes_read);
        if (failed(error)) return error;
      }
      if (read_result.end_of_stream) break;
      if (read_result.bytes_read == 0)
        return make_error(storage_error_code::backend_failure, "object reader made no progress");
    }

    std::string checksum_value;
    error = checksum_hasher->finish(checksum_value);
    if (failed(error)) return error;
    recalculated_checksum = object_checksum{std::move(requested_checksum_algorithm), std::move(checksum_value),
                                            object_checksum_type::full_object};
  }

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  indexed_object source;
  error = options.source_version_id.has_value()
              ? m_index.find_object_version(source_bucket, source_key, *options.source_version_id, source)
              : m_index.find_object(source_bucket, source_key, source);
  if (options.source_version_id.has_value() && error.code == storage_error_code::object_not_found)
    error = make_error(storage_error_code::object_version_not_found, "source object version was not found");
  if (failed(error)) return error;
  if (source.is_corrupted)
    return make_error(storage_error_code::object_corrupted, "source object payload is corrupted");
  if (source.is_delete_marker)
    return make_error(storage_error_code::object_is_delete_marker, "source object version is a delete marker");
  if (validated_source_etag.has_value() && source.etag != *validated_source_etag)
    return make_error(storage_error_code::precondition_failed, "source object changed during checksum verification");
  error = check_write_conditions(source, options.source_conditions);
  if (error.code == storage_error_code::not_modified) return {storage_error_code::precondition_failed, error.message};
  if (failed(error)) return error;

  indexed_object target = source;
  target.bucket = target_bucket;
  target.key = target_key;
  bucket_info target_bucket_info;
  error = m_index.find_bucket(target_bucket, target_bucket_info);
  if (failed(error)) return error;
  target.version_id = make_write_version_id(target_bucket_info.versioning);
  target.created_at = std::chrono::system_clock::now();
  target.modified_at = target.created_at;
  target.generation = 0;
  if (options.replace_metadata) {
    const std::optional<object_checksum> source_checksum =
        recalculated_checksum.has_value() ? recalculated_checksum : target.metadata.checksum;
    target.metadata = replacement_metadata;
    target.metadata.content_length = source.metadata.content_length;
    if (!target.metadata.checksum.has_value()) target.metadata.checksum = source_checksum;
  } else if (recalculated_checksum.has_value()) {
    target.metadata.checksum = recalculated_checksum;
  }
  if (!target.metadata.checksum.has_value()) target.metadata.checksum = target.payload.internal_checksum;

  error = m_index.publish_object(target, options.target_conditions);
  if (failed(error)) return error;

  m_object_cache.erase(target_bucket, target_key);
  result.source_version_id = source.version_id;
  result.etag = target.etag;
  result.version_id = target.version_id;
  result.checksum = *target.metadata.checksum;
  result.modified_at = target.modified_at;
  return {};
}

} // namespace extora::core
