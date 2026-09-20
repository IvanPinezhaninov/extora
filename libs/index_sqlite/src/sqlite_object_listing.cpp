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

#include "sqlite_object_records.h"

#include <limits>
#include <string>
#include <utility>

#include "sqlite_bucket_records.h"
#include "sqlite_common.h"
#include "sqlite_object_record_support.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

storage_error list_object_records(sqlite3* database, const bucket_name& bucket, const list_objects_options& options,
                                  indexed_object_page& result)
{
  result = {};

  bucket_info info;
  storage_error error = find_bucket_record(database, bucket, info);
  if (failed(error)) return error;

  if (options.max_keys == 0) return {};

  statement query{database, R"sql(
    SELECT
      bucket.name,
      obj.key,
      payload.content_length,
      obj.content_type,
      obj.cache_control,
      obj.content_disposition,
      obj.content_encoding,
      obj.content_language,
      obj.expires_at,
      obj.checksum_algorithm,
      obj.checksum_value,
      obj.checksum_type,
      obj.etag,
      obj.version_id,
      obj.created_at,
      obj.modified_at,
      obj.generation,
      obj.id
    FROM objects AS obj
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    JOIN object_payloads AS payload ON payload.id = obj.payload_id
    WHERE bucket.name = ?1
      AND obj.state = ?2
      AND obj.is_delete_marker = 0
      AND (?3 = '' OR obj.key >= ?3)
      AND (?4 = '' OR obj.key > ?4)
    ORDER BY obj.key
    LIMIT ?5
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare list objects");

  const std::size_t max_sqlite_limit = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
  const std::int64_t row_limit = options.max_keys >= max_sqlite_limit ? std::numeric_limits<std::int64_t>::max()
                                                                      : static_cast<std::int64_t>(options.max_keys + 1);

  error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::current);
  if (!failed(error)) error = bind_text(database, query.get(), 3, options.prefix);
  const std::string marker = options.continuation_token.empty() ? options.start_after : options.continuation_token;
  if (!failed(error)) error = bind_text(database, query.get(), 4, marker);
  if (!failed(error)) error = bind_int64(database, query.get(), 5, row_limit);
  if (failed(error)) return error;

  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) break;

    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list objects");

    indexed_object object;
    object.bucket.value = column_text(query.get(), 0);
    object.key.value = column_text(query.get(), 1);
    object.metadata.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));

    if (!options.prefix.empty() && object.key.value.compare(0, options.prefix.size(), options.prefix) != 0) break;

    if (result.objects.size() == options.max_keys) {
      result.is_truncated = true;
      break;
    }

    object.metadata.content_type = column_optional_text(query.get(), 3);
    object.metadata.cache_control = column_optional_text(query.get(), 4);
    object.metadata.content_disposition = column_optional_text(query.get(), 5);
    object.metadata.content_encoding = column_optional_text(query.get(), 6);
    object.metadata.content_language = column_optional_text(query.get(), 7);
    object.metadata.expires_at = column_optional_time_point(query.get(), 8);
    object.metadata.checksum = column_optional_checksum(query.get(), 9, 10, 11);

    object.etag = column_text(query.get(), 12);
    object.version_id.value = column_is_null(query.get(), 13) ? null_version_id : column_text(query.get(), 13);
    object.is_latest = true;
    object.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 14));
    object.modified_at = from_unix_ms(sqlite3_column_int64(query.get(), 15));
    object.generation = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 16));
    error = object_record_detail::load_custom_metadata(database, sqlite3_column_int64(query.get(), 17), object);
    if (failed(error)) return error;

    result.next_continuation_token = object.key.value;
    result.objects.push_back(std::move(object));
  }

  if (!result.is_truncated) result.next_continuation_token.reset();

  return {};
}

storage_error list_object_version_records(sqlite3* database, const bucket_name& bucket,
                                          const list_object_versions_options& options,
                                          indexed_object_version_page& result)
{
  result = {};
  bucket_info info;
  storage_error error = find_bucket_record(database, bucket, info);
  if (failed(error) || options.max_keys == 0) return error;

  statement query{database, R"sql(
    SELECT
      obj.key, payload.content_length, obj.content_type, obj.cache_control,
      obj.content_disposition, obj.content_encoding, obj.content_language, obj.expires_at,
      obj.checksum_algorithm, obj.checksum_value, obj.checksum_type, obj.etag, obj.version_id, obj.is_delete_marker,
      obj.created_at, obj.modified_at, obj.generation, obj.state, obj.id
    FROM objects AS obj
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    LEFT JOIN object_payloads AS payload ON payload.id = obj.payload_id
    WHERE bucket.name = ?1
      AND obj.state != ?2
      AND obj.state != ?3
      AND (?4 = '' OR obj.key >= ?4)
    ORDER BY obj.key, obj.generation DESC
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object version list");
  error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::deleted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::corrupted);
  if (!failed(error)) error = bind_text(database, query.get(), 4, options.prefix);
  if (failed(error)) return error;

  bool after_token = options.key_marker.empty();
  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) break;
    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list object versions");
    const std::string version_value = column_is_null(query.get(), 12) ? null_version_id : column_text(query.get(), 12);
    const std::string key_value = column_text(query.get(), 0);
    if (!after_token) {
      if (key_value > options.key_marker)
        after_token = true;
      else if (key_value == options.key_marker && options.version_id_marker.has_value() &&
               version_value == options.version_id_marker->value) {
        after_token = true;
        continue;
      } else {
        continue;
      }
    }
    if (!options.prefix.empty() && key_value.compare(0, options.prefix.size(), options.prefix) != 0) break;
    if (result.objects.size() == options.max_keys) {
      result.is_truncated = true;
      break;
    }
    indexed_object object;
    object.bucket = bucket;
    object.key.value = key_value;
    object.metadata.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    object.metadata.content_type = column_optional_text(query.get(), 2);
    object.metadata.cache_control = column_optional_text(query.get(), 3);
    object.metadata.content_disposition = column_optional_text(query.get(), 4);
    object.metadata.content_encoding = column_optional_text(query.get(), 5);
    object.metadata.content_language = column_optional_text(query.get(), 6);
    object.metadata.expires_at = column_optional_time_point(query.get(), 7);
    object.metadata.checksum = column_optional_checksum(query.get(), 8, 9, 10);
    object.etag = column_text(query.get(), 11);
    object.version_id.value = version_value;
    object.is_delete_marker = sqlite3_column_int(query.get(), 13) != 0;
    object.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 14));
    object.modified_at = from_unix_ms(sqlite3_column_int64(query.get(), 15));
    object.generation = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 16));
    object.is_latest = sqlite3_column_int64(query.get(), 17) == to_int(object_state::current);
    error = object_record_detail::load_custom_metadata(database, sqlite3_column_int64(query.get(), 18), object);
    if (failed(error)) return error;
    result.next_key_marker = object.key.value;
    result.next_version_id_marker = object.version_id;
    result.objects.push_back(std::move(object));
  }
  if (!result.is_truncated) {
    result.next_key_marker.reset();
    result.next_version_id_marker.reset();
  }
  return {};
}

} // namespace extora::core::sqlite_detail
