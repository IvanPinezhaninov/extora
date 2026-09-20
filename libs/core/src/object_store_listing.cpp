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
#include <string>
#include <utility>

#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "extora/core/opaque_id.h"
#include "object_store_core_helpers.h"

namespace extora::core {

using object_store_detail::make_delete_marker_version_id;
using object_store_detail::to_object_info;
using object_store_detail::validate_bucket_name;
using object_store_detail::validate_object_address;

storage_error object_store_core::delete_object(const bucket_name& bucket, const object_key& key,
                                               delete_object_result& result, const delete_object_options& options)
{
  result = {};
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) return address_validation_error;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  bucket_info bucket_state;
  storage_error error = m_index.find_bucket(bucket, bucket_state);
  if (failed(error)) return error;
  error = m_index.delete_object(bucket, key, options, make_delete_marker_version_id(bucket_state.versioning), result);
  if (options.version_id.has_value() && error.code == storage_error_code::object_not_found)
    error = make_error(storage_error_code::object_version_not_found, "object version was not found");
  if (!failed(error)) m_object_cache.erase(bucket, key);
  return error;
}

storage_error object_store_core::list_object_versions(const bucket_name& bucket, object_version_list& result,
                                                      const list_object_versions_options& options)
{
  result = {};
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;
  list_object_versions_options index_options = options;
  index_options.max_keys = (std::min)(options.max_keys, max_list_page_size);
  if (options.delimiter.empty()) {
    indexed_object_version_page page;
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.list_object_versions(bucket, index_options, page);
    if (failed(error)) return error;
    if (page.is_truncated && (!page.next_key_marker.has_value() || !page.next_version_id_marker.has_value()))
      return make_error(storage_error_code::index_failure, "truncated version page did not provide next markers");
    result.is_truncated = page.is_truncated;
    result.next_key_marker = std::move(page.next_key_marker);
    result.next_version_id_marker = std::move(page.next_version_id_marker);
    result.versions.reserve(page.objects.size());
    for (const indexed_object& object : page.objects)
      result.versions.push_back(to_object_info(object));
    return {};
  }

  constexpr std::size_t list_batch_size = 1024;
  list_object_versions_options batch_options = index_options;
  batch_options.delimiter.clear();
  batch_options.max_keys = options.max_keys == 0 ? 0 : list_batch_size;
  std::size_t entry_count = 0;
  std::string last_common_prefix;
  std::string page_key_marker;
  object_version_id page_version_marker;
  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  while (true) {
    indexed_object_version_page batch;
    const storage_error error = m_index.list_object_versions(bucket, batch_options, batch);
    if (failed(error)) return error;
    if (batch.objects.empty()) return {};

    for (const indexed_object& object : batch.objects) {
      const std::size_t delimiter_position = object.key.value.find(options.delimiter, options.prefix.size());
      const std::string common_prefix = delimiter_position == std::string::npos
                                            ? std::string{}
                                            : object.key.value.substr(0, delimiter_position + options.delimiter.size());
      if (!common_prefix.empty() && !options.key_marker.empty() && common_prefix <= options.key_marker) {
        last_common_prefix = common_prefix;
        page_key_marker = object.key.value;
        page_version_marker = object.version_id;
        continue;
      }
      if (!common_prefix.empty() && common_prefix == last_common_prefix) {
        page_key_marker = object.key.value;
        page_version_marker = object.version_id;
        continue;
      }
      if (entry_count == index_options.max_keys) {
        result.is_truncated = true;
        result.next_key_marker = page_key_marker;
        result.next_version_id_marker = page_version_marker;
        return {};
      }
      if (common_prefix.empty()) {
        result.versions.push_back(to_object_info(object));
        last_common_prefix.clear();
      } else {
        result.common_prefixes.push_back(common_prefix);
        last_common_prefix = common_prefix;
      }
      page_key_marker = object.key.value;
      page_version_marker = object.version_id;
      ++entry_count;
    }
    if (!batch.is_truncated) return {};
    if (!batch.next_key_marker.has_value() || !batch.next_version_id_marker.has_value())
      return make_error(storage_error_code::index_failure, "truncated version page did not provide next markers");
    batch_options.key_marker = std::move(*batch.next_key_marker);
    batch_options.version_id_marker = std::move(*batch.next_version_id_marker);
  }
}

storage_error object_store_core::list_objects(const bucket_name& bucket, object_list& result,
                                              const list_objects_options& options)
{
  result = {};
  const storage_error bucket_validation_error = validate_bucket_name(bucket);
  if (failed(bucket_validation_error)) return bucket_validation_error;

  list_objects_options index_options = options;
  index_options.max_keys = (std::min)(options.max_keys, max_list_page_size);
  if (!options.continuation_token.empty() &&
      !decode_listing_token(options.continuation_token, object_listing_token_prefix, index_options.continuation_token))
    return make_error(storage_error_code::invalid_continuation_token, "invalid object continuation token");

  if (options.delimiter.empty()) {
    indexed_object_page page;
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.list_objects(bucket, index_options, page);
    if (failed(error)) return error;
    if (page.is_truncated && !page.next_continuation_token.has_value())
      return make_error(storage_error_code::index_failure,
                        "truncated object page did not provide a continuation token");

    result.is_truncated = page.is_truncated;
    if (page.next_continuation_token.has_value())
      result.next_continuation_token = encode_listing_token(object_listing_token_prefix, *page.next_continuation_token);
    result.objects.reserve(page.objects.size());
    for (const indexed_object& object : page.objects)
      result.objects.push_back(to_object_info(object));
    return {};
  }

  constexpr std::size_t list_batch_size = 1024;
  list_objects_options batch_options = index_options;
  batch_options.delimiter.clear();
  batch_options.max_keys = options.max_keys == 0 ? 0 : list_batch_size;

  std::size_t entry_count = 0;
  std::string last_common_prefix;
  std::string page_token;
  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  while (true) {
    indexed_object_page batch;
    const storage_error error = m_index.list_objects(bucket, batch_options, batch);
    if (failed(error)) return error;
    if (batch.objects.empty()) return {};

    for (const indexed_object& object : batch.objects) {
      const std::size_t delimiter_position = object.key.value.find(options.delimiter, options.prefix.size());
      const std::string common_prefix = delimiter_position == std::string::npos
                                            ? std::string{}
                                            : object.key.value.substr(0, delimiter_position + options.delimiter.size());

      if (!common_prefix.empty() && options.continuation_token.empty() && !options.start_after.empty() &&
          common_prefix <= options.start_after) {
        last_common_prefix = common_prefix;
        page_token = object.key.value;
        continue;
      }

      if (!common_prefix.empty() && common_prefix == last_common_prefix) {
        page_token = object.key.value;
        continue;
      }

      if (entry_count == index_options.max_keys) {
        result.is_truncated = true;
        result.next_continuation_token = encode_listing_token(object_listing_token_prefix, page_token);
        return {};
      }

      if (common_prefix.empty()) {
        result.objects.push_back(to_object_info(object));
        last_common_prefix.clear();
      } else {
        result.common_prefixes.push_back(common_prefix);
        last_common_prefix = common_prefix;
      }
      page_token = object.key.value;
      ++entry_count;
    }

    if (!batch.is_truncated) return {};
    if (!batch.next_continuation_token.has_value())
      return make_error(storage_error_code::index_failure,
                        "truncated object page did not provide a continuation token");
    batch_options.continuation_token = std::move(*batch.next_continuation_token);
  }
}

} // namespace extora::core
