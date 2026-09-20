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

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "extora/core/opaque_id.h"
#include "object_store_core_helpers.h"
#include "operation_tracker.h"
#include "payload_write.h"

namespace extora::core {

using object_store_detail::make_write_version_id;
using object_store_detail::validate_bucket_name;
using object_store_detail::validate_object_address;

namespace {

constexpr std::uint32_t multipart_max_part_number = 10000;

storage_error validate_multipart_upload_id(const multipart_upload_id& upload_id)
{
  if (upload_id.value.empty())
    return make_error(storage_error_code::multipart_upload_not_found, "multipart upload id is empty");

  return {};
}

storage_error validate_part_number(std::uint32_t part_number)
{
  if (part_number == 0 || part_number > multipart_max_part_number)
    return make_error(storage_error_code::invalid_part, "multipart part number is outside the valid range");

  return {};
}

storage_error verify_multipart_target(const indexed_multipart_upload& upload, const bucket_name& bucket,
                                      const object_key& key)
{
  if (upload.bucket.value == bucket.value && upload.key.value == key.value) return {};
  return make_error(storage_error_code::multipart_upload_not_found, "multipart upload target does not match");
}

bool valid_checksum_type(object_checksum_type type)
{
  return type == object_checksum_type::full_object || type == object_checksum_type::composite;
}

} // namespace

storage_error object_store_core::create_multipart_upload(const bucket_name& bucket, const object_key& key,
                                                         const object_metadata& metadata,
                                                         create_multipart_upload_result& result,
                                                         const create_multipart_upload_options& options)
{
  result = {};
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) return address_validation_error;
  if (options.checksum_algorithm.value.empty())
    return make_error(storage_error_code::unsupported_checksum_algorithm, "checksum algorithm is empty");
  if (!valid_checksum_type(options.checksum_type))
    return make_error(storage_error_code::unsupported_checksum_type, "unsupported multipart checksum type");

  storage_error hasher_error;
  std::unique_ptr<hasher> checksum_hasher = m_hash_factory.create_hasher(options.checksum_algorithm, hasher_error);
  if (failed(hasher_error)) return hasher_error;
  if (!checksum_hasher)
    return make_error(storage_error_code::unsupported_checksum_algorithm, "unsupported multipart checksum algorithm");
  if (options.checksum_type == object_checksum_type::composite &&
      !m_hash_factory.can_combine_checksums(options.checksum_algorithm)) {
    return make_error(storage_error_code::unsupported_checksum_type,
                      "composite checksum is unsupported for the selected algorithm");
  }

  indexed_multipart_upload upload;
  upload.initiated_at = std::chrono::system_clock::now();
  upload.upload_id.value = generate_ordered_opaque_id(multipart_upload_id_prefix, upload.initiated_at);
  upload.bucket = bucket;
  upload.key = key;
  upload.metadata.content_type = metadata.content_type;
  upload.metadata.cache_control = metadata.cache_control;
  upload.metadata.content_disposition = metadata.content_disposition;
  upload.metadata.content_encoding = metadata.content_encoding;
  upload.metadata.content_language = metadata.content_language;
  upload.metadata.expires_at = metadata.expires_at;
  upload.metadata.custom_metadata = metadata.custom_metadata;
  upload.checksum_algorithm = options.checksum_algorithm;
  upload.checksum_type = options.checksum_type;
  upload.dedup = options.dedup;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  const storage_error error = m_index.create_multipart_upload(upload);
  if (failed(error)) return error;
  result.upload_id = upload.upload_id;
  result.checksum_algorithm = upload.checksum_algorithm;
  result.checksum_type = upload.checksum_type;
  return {};
}

storage_error object_store_core::upload_part(const bucket_name& bucket, const object_key& key,
                                             const multipart_upload_id& upload_id, std::uint32_t part_number,
                                             object_reader& reader, upload_part_result& result,
                                             const upload_part_options& options)
{
  operation_tracker operation{
      m_observer.get(), m_next_operation_id, m_operation_progress_interval_bytes, operation_type::upload_part,
      bucket.value,     key.value,           options.expected_content_length,     std::string_view{upload_id.value},
      part_number};
  const storage_error error =
      upload_part_with_tracker(bucket, key, upload_id, part_number, reader, options, result, operation);
  operation.finish(error);
  return error;
}

storage_error object_store_core::upload_part_with_tracker(const bucket_name& bucket, const object_key& key,
                                                          const multipart_upload_id& upload_id,
                                                          std::uint32_t part_number, object_reader& reader,
                                                          const upload_part_options& options,
                                                          upload_part_result& result, operation_tracker& operation)
{
  result = {};
  storage_error error = validate_object_address(bucket, key);
  if (failed(error)) return error;
  error = validate_multipart_upload_id(upload_id);
  if (failed(error)) return error;
  error = validate_part_number(part_number);
  if (failed(error)) return error;

  operation_limiter::permit write_permit = m_write_limiter.try_acquire();
  if (!write_permit.acquired()) {
    return make_error(storage_error_code::concurrency_limit_exceeded, "maximum concurrent writes reached");
  }

  indexed_multipart_upload upload;
  {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    error = m_index.find_multipart_upload(upload_id, upload);
    if (failed(error)) return error;
    error = verify_multipart_target(upload, bucket, key);
    if (failed(error)) return error;
  }

  payload_write_options write_options;
  write_options.expected_content_length = options.expected_content_length;
  if (options.expected_checksum.has_value()) {
    write_options.expected_checksum =
        object_checksum{upload.checksum_algorithm, *options.expected_checksum, object_checksum_type::full_object};
  }
  write_options.public_checksum_algorithm = upload.checksum_algorithm;
  write_options.operation = &operation;

  payload_write_result write_result;
  error = write_payload(reader, std::move(write_options), write_result);
  if (failed(error)) return error;

  const auto abandon_written_extents = [&]() -> storage_error {
    if (write_result.extents.empty()) return {};

    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    return m_index.abandon_extents(write_result.extents);
  };

  indexed_multipart_part part;
  part.info.part_number = part_number;
  part.info.etag = write_result.etag;
  part.info.content_length = write_result.content_length;
  part.info.checksum = write_result.checksum;
  part.info.created_at = std::chrono::system_clock::now();
  part.extents = std::move(write_result.extents);
  part.internal_checksum = std::move(write_result.internal_checksum);

  {
    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    error = m_index.store_multipart_part(upload_id, bucket, key, part);
  }
  if (failed(error)) {
    write_result.extents = std::move(part.extents);
    static_cast<void>(abandon_written_extents());
    return error;
  }
  result.etag = part.info.etag;
  result.content_length = part.info.content_length;
  result.checksum = *part.info.checksum;
  result.created_at = part.info.created_at;

  return {};
}

storage_error object_store_core::complete_multipart_upload(const bucket_name& bucket, const object_key& key,
                                                           const multipart_upload_id& upload_id,
                                                           const complete_multipart_upload_options& options,
                                                           put_object_result& result)
{
  result = {};
  storage_error error = validate_object_address(bucket, key);
  if (failed(error)) return error;
  error = validate_multipart_upload_id(upload_id);
  if (failed(error)) return error;
  if (options.parts.empty()) return make_error(storage_error_code::invalid_part, "multipart completion has no parts");

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  indexed_multipart_upload upload;
  error = m_index.find_multipart_upload(upload_id, upload);
  if (failed(error)) return error;
  error = verify_multipart_target(upload, bucket, key);
  if (failed(error)) return error;

  std::uint32_t previous_part_number = 0;
  for (const completed_multipart_part& completed_part : options.parts) {
    error = validate_part_number(completed_part.part_number);
    if (failed(error)) return error;
    if (completed_part.part_number <= previous_part_number)
      return make_error(storage_error_code::invalid_part_order, "multipart parts are not in ascending order");
    if (upload.checksum_type == object_checksum_type::composite &&
        completed_part.part_number != previous_part_number + 1) {
      return make_error(storage_error_code::invalid_part_order,
                        "composite checksum parts must be consecutive and start at one");
    }
    previous_part_number = completed_part.part_number;
  }

  std::uint64_t total_bytes = 0;
  std::vector<physical_extent> extents;
  std::vector<object_part_info> completed_parts;
  std::vector<std::string_view> part_checksum_values;
  completed_parts.reserve(options.parts.size());
  for (std::size_t index = 0; index < options.parts.size(); ++index) {
    const completed_multipart_part& completed_part = options.parts[index];
    const auto part = std::find_if(upload.parts.begin(), upload.parts.end(), [&](const indexed_multipart_part& value) {
      return value.info.part_number == completed_part.part_number;
    });
    if (part == upload.parts.end())
      return make_error(storage_error_code::invalid_part, "completed multipart part was not uploaded");
    error = verify_payload_integrity(part->extents, part->internal_checksum);
    if (failed(error)) return error;
    if (part->info.etag != completed_part.etag)
      return make_error(storage_error_code::invalid_part, "completed multipart part ETag does not match");
    if (!part->info.checksum.has_value() ||
        part->info.checksum->checksum_algorithm.value != upload.checksum_algorithm.value ||
        part->info.checksum->type != object_checksum_type::full_object)
      return make_error(storage_error_code::invalid_part, "uploaded part checksum does not match upload contract");
    if (completed_part.expected_checksum.has_value()) {
      if (!part->info.checksum.has_value() || *completed_part.expected_checksum != part->info.checksum->value)
        return make_error(storage_error_code::invalid_part, "completed multipart part checksum does not match");
    }
    if (index + 1 < options.parts.size() && part->info.content_length < m_multipart_min_part_size) {
      return make_error(storage_error_code::part_too_small,
                        "completed multipart part is smaller than configured minimum of " +
                            std::to_string(m_multipart_min_part_size) + " bytes");
    }

    completed_parts.push_back(
        object_part_info{part->info.part_number, total_bytes, part->info.content_length, *part->info.checksum});
    total_bytes += part->info.content_length;
    extents.insert(extents.end(), part->extents.begin(), part->extents.end());
    part_checksum_values.push_back(part->info.checksum->value);
  }

  stored_object_metadata metadata = object_store_detail::make_stored_object_metadata(upload.metadata);
  metadata.content_length = total_bytes;

  const bool use_dedup = upload.dedup == dedup_mode::enabled && total_bytes >= m_dedup_min_object_size;
  std::string internal_checksum_value;
  error = hash_payload_extents(extents, xxh3_128_checksum_algorithm, storage_error_code::backend_failure,
                               "internal checksum hasher is unavailable", internal_checksum_value);
  if (failed(error)) return error;

  bucket_info bucket_state;
  error = m_index.find_bucket(bucket, bucket_state);
  if (failed(error)) return error;

  indexed_object object;
  object.bucket = bucket;
  object.key = key;
  object.parts = std::move(completed_parts);
  object.etag = xxh3_128_checksum_algorithm.value + ":" + internal_checksum_value;
  object.version_id = make_write_version_id(bucket_state.versioning);
  object.payload.extents = extents;
  object.payload.internal_checksum = object_checksum{xxh3_128_checksum_algorithm, std::move(internal_checksum_value)};
  object_checksum public_checksum;
  public_checksum.checksum_algorithm = upload.checksum_algorithm;
  public_checksum.type = upload.checksum_type;
  if (upload.checksum_type == object_checksum_type::full_object) {
    if (public_checksum.checksum_algorithm.value == object.payload.internal_checksum.checksum_algorithm.value) {
      public_checksum.value = object.payload.internal_checksum.value;
    } else {
      error = hash_payload_extents(extents, public_checksum.checksum_algorithm,
                                   storage_error_code::unsupported_checksum_algorithm,
                                   "multipart checksum hasher is unavailable", public_checksum.value);
      if (failed(error)) return error;
    }
  } else {
    error = m_hash_factory.combine_checksums(public_checksum.checksum_algorithm, part_checksum_values,
                                             public_checksum.value);
    if (failed(error)) return error;
  }
  if (options.expected_checksum.has_value() && *options.expected_checksum != public_checksum.value)
    return make_error(storage_error_code::checksum_mismatch, "completed multipart checksum does not match");
  metadata.checksum = std::move(public_checksum);
  if (use_dedup) {
    object.payload.dedup_algorithm = xxh3_128_checksum_algorithm;
    object.payload.dedup_value = object.payload.internal_checksum.value;
  }
  object.metadata = metadata;
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;

  if (use_dedup) {
    indexed_object duplicate;
    error = m_index.find_dedup_object(xxh3_128_checksum_algorithm, object.payload.internal_checksum.value, total_bytes,
                                      duplicate);
    if (!failed(error)) {
      object.payload = duplicate.payload;
    } else if (error.code != storage_error_code::object_not_found) {
      return error;
    }
  }
  error = m_index.complete_multipart_upload(upload_id, object, options.conditions);
  if (failed(error)) return error;

  m_object_cache.erase(bucket, key);
  result.etag = object.etag;
  result.version_id = object.version_id;
  result.checksum = *object.metadata.checksum;

  return {};
}

storage_error object_store_core::abort_multipart_upload(const bucket_name& bucket, const object_key& key,
                                                        const multipart_upload_id& upload_id)
{
  storage_error error = validate_object_address(bucket, key);
  if (failed(error)) return error;
  error = validate_multipart_upload_id(upload_id);
  if (failed(error)) return error;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.abort_multipart_upload(upload_id, bucket, key);
}

storage_error object_store_core::list_parts(const bucket_name& bucket, const object_key& key,
                                            const multipart_upload_id& upload_id, multipart_part_list& result,
                                            const list_parts_options& options)
{
  result = {};
  storage_error error = validate_object_address(bucket, key);
  if (failed(error)) return error;
  error = validate_multipart_upload_id(upload_id);
  if (failed(error)) return error;
  const std::size_t max_parts = (std::min)(options.max_parts, max_list_page_size);
  if (max_parts == 0) return {};

  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  indexed_multipart_upload upload;
  error = m_index.find_multipart_upload(upload_id, upload);
  if (failed(error)) return error;
  error = verify_multipart_target(upload, bucket, key);
  if (failed(error)) return error;

  for (const indexed_multipart_part& part : upload.parts) {
    if (part.info.part_number <= options.part_number_marker) continue;
    if (result.parts.size() == max_parts) {
      result.is_truncated = true;
      result.next_part_number_marker = result.parts.back().part_number;
      break;
    }
    result.parts.push_back(part.info);
  }
  return {};
}

storage_error object_store_core::list_multipart_uploads(const bucket_name& bucket, multipart_upload_list& result,
                                                        const list_multipart_uploads_options& options)
{
  result = {};
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;
  if (options.max_uploads == 0 || options.max_uploads > max_list_page_size)
    return make_error(storage_error_code::invalid_list_options, "max_uploads must be between 1 and 1000");

  std::vector<indexed_multipart_upload> uploads;
  {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.list_multipart_uploads(bucket, uploads);
    if (failed(error)) return error;
  }

  std::string last_common_prefix;
  for (const indexed_multipart_upload& upload : uploads) {
    if (!options.prefix.empty() && upload.key.value.compare(0, options.prefix.size(), options.prefix) != 0) continue;
    if (!options.key_marker.empty()) {
      if (upload.key.value < options.key_marker) continue;
      if (upload.key.value == options.key_marker && !options.upload_id_marker.value.empty() &&
          upload.upload_id.value <= options.upload_id_marker.value)
        continue;
      if (upload.key.value == options.key_marker && options.upload_id_marker.value.empty()) continue;
    }
    const std::size_t delimiter_position =
        options.delimiter.empty() ? std::string::npos : upload.key.value.find(options.delimiter, options.prefix.size());
    const std::string common_prefix = delimiter_position == std::string::npos
                                          ? std::string{}
                                          : upload.key.value.substr(0, delimiter_position + options.delimiter.size());
    if (!common_prefix.empty() && !options.key_marker.empty() && common_prefix <= options.key_marker) {
      last_common_prefix = common_prefix;
      result.next_key_marker = upload.key.value;
      result.next_upload_id_marker = upload.upload_id;
      continue;
    }
    if (!common_prefix.empty() && common_prefix == last_common_prefix) {
      result.next_key_marker = upload.key.value;
      result.next_upload_id_marker = upload.upload_id;
      continue;
    }
    if (result.uploads.size() + result.common_prefixes.size() == options.max_uploads) {
      result.is_truncated = true;
      break;
    }

    if (common_prefix.empty()) {
      result.uploads.push_back(multipart_upload_info{upload.key, upload.upload_id, upload.checksum_algorithm,
                                                     upload.checksum_type, upload.initiated_at});
      last_common_prefix.clear();
    } else {
      result.common_prefixes.push_back(common_prefix);
      last_common_prefix = common_prefix;
    }
    result.next_key_marker = upload.key.value;
    result.next_upload_id_marker = upload.upload_id;
  }
  if (!result.is_truncated) {
    result.next_key_marker.reset();
    result.next_upload_id_marker.reset();
  }
  return {};
}

} // namespace extora::core
