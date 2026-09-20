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

#include <cstdint>
#include <limits>
#include <utility>

#include "sqlite_common.h"
#include "sqlite_object_record_support.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

enum class record_contents : std::uint8_t {
  full,
  metadata,
};

storage_error load_object_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                 const object_version_id* version_id, record_contents contents, indexed_object& result)
{
  statement query{database, R"sql(
    SELECT
      bucket.name,
      obj.id,
      obj.key,
      payload.id,
      payload.content_length,
      payload.internal_checksum_algorithm,
      payload.internal_checksum_value,
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
      obj.is_delete_marker,
      payload.dedup_algorithm,
      payload.dedup_value,
      obj.created_at,
      obj.modified_at,
      obj.generation,
      obj.state
    FROM buckets AS bucket
    LEFT JOIN objects AS obj ON obj.bucket_id = bucket.id
      AND obj.key = ?2
      AND (
        (?4 = 0 AND (obj.state = ?3 OR obj.state = ?8))
        OR (?4 = 1 AND ?5 = ?7 AND obj.version_id IS NULL AND obj.state != ?6)
        OR (?4 = 1 AND ?5 != ?7 AND obj.version_id = ?5 AND obj.state != ?6)
      )
    LEFT JOIN object_payloads AS payload ON payload.id = obj.payload_id
    WHERE bucket.name = ?1
    ORDER BY obj.generation DESC
    LIMIT 1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare find object");

  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_text(database, query.get(), 2, key.value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::current);
  if (!failed(error)) error = bind_int64(database, query.get(), 4, version_id == nullptr ? 0 : 1);
  if (!failed(error)) error = bind_text(database, query.get(), 5, version_id == nullptr ? "" : version_id->value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 6, object_state::deleted);
  if (!failed(error)) error = bind_text(database, query.get(), 7, null_version_id);
  if (!failed(error)) error = bind_object_state(database, query.get(), 8, object_state::corrupted);
  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::bucket_not_found, "bucket not found");

  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find object");

  if (column_is_null(query.get(), 1)) return make_error(storage_error_code::object_not_found, "object not found");

  const std::int64_t object_id = sqlite3_column_int64(query.get(), 1);

  result = indexed_object{};
  result.bucket.value = column_text(query.get(), 0);
  result.key.value = column_text(query.get(), 2);
  result.payload.id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 3));
  result.payload.extents.clear();
  result.metadata.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 4));
  result.payload.internal_checksum =
      object_checksum{checksum_algorithm_name{column_text(query.get(), 5)}, column_text(query.get(), 6)};

  result.metadata.content_type = column_optional_text(query.get(), 7);
  result.metadata.cache_control = column_optional_text(query.get(), 8);
  result.metadata.content_disposition = column_optional_text(query.get(), 9);
  result.metadata.content_encoding = column_optional_text(query.get(), 10);
  result.metadata.content_language = column_optional_text(query.get(), 11);
  result.metadata.expires_at = column_optional_time_point(query.get(), 12);
  result.metadata.checksum = column_optional_checksum(query.get(), 13, 14, 15);

  result.etag = column_text(query.get(), 16);
  result.version_id.value = column_is_null(query.get(), 17) ? null_version_id : column_text(query.get(), 17);
  result.is_delete_marker = sqlite3_column_int(query.get(), 18) != 0;
  const std::int64_t state = sqlite3_column_int64(query.get(), 24);
  result.is_latest =
      state == to_int(object_state::current) || (version_id == nullptr && state == to_int(object_state::corrupted));
  result.is_corrupted = state == to_int(object_state::corrupted);
  if (!column_is_null(query.get(), 19))
    result.payload.dedup_algorithm = checksum_algorithm_name{column_text(query.get(), 19)};
  if (!column_is_null(query.get(), 20)) result.payload.dedup_value = column_text(query.get(), 20);
  result.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 21));
  result.modified_at = from_unix_ms(sqlite3_column_int64(query.get(), 22));
  result.generation = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 23));

  error = object_record_detail::load_custom_metadata(database, object_id, result);
  if (failed(error) || contents == record_contents::metadata || result.is_delete_marker) return error;
  return object_record_detail::load_payload_extents(database, static_cast<std::int64_t>(result.payload.id), result);
}

} // namespace

storage_error find_object_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                 indexed_object& result)
{
  return load_object_record(database, bucket, key, nullptr, record_contents::full, result);
}

storage_error find_object_version_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                         const object_version_id& version_id, indexed_object& result)
{
  return load_object_record(database, bucket, key, &version_id, record_contents::full, result);
}

storage_error find_object_metadata_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                          indexed_object& result)
{
  return load_object_record(database, bucket, key, nullptr, record_contents::metadata, result);
}

storage_error list_object_part_records(sqlite3* database, const indexed_object& object,
                                       std::uint32_t part_number_marker, std::size_t max_parts,
                                       indexed_object_part_page& result)
{
  result = {};
  statement count_query{database, R"sql(
    SELECT COUNT(*)
    FROM object_parts AS part
    JOIN objects AS obj ON obj.id = part.object_id
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    WHERE bucket.name = ?1
      AND obj.key = ?2
      AND obj.generation = ?3
      AND obj.state != ?4
    )sql"};
  if (!count_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object part count");

  storage_error error = bind_text(database, count_query.get(), 1, object.bucket.value);
  if (!failed(error)) error = bind_text(database, count_query.get(), 2, object.key.value);
  if (!failed(error)) error = bind_uint64(database, count_query.get(), 3, object.generation);
  if (!failed(error)) error = bind_object_state(database, count_query.get(), 4, object_state::deleted);
  if (failed(error)) return error;
  if (count_query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to count object parts");

  result.total_parts = static_cast<std::uint32_t>(sqlite3_column_int64(count_query.get(), 0));
  if (max_parts == 0) return {};

  statement query{database, R"sql(
    SELECT
      part.part_number,
      part.offset,
      part.content_length,
      part.checksum_algorithm,
      part.checksum_value,
      part.checksum_type
    FROM object_parts AS part
    JOIN objects AS obj ON obj.id = part.object_id
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    WHERE bucket.name = ?1
      AND obj.key = ?2
      AND obj.generation = ?3
      AND obj.state != ?4
      AND part.part_number > ?5
    ORDER BY part.part_number
    LIMIT ?6
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object part listing");

  error = bind_text(database, query.get(), 1, object.bucket.value);
  if (!failed(error)) error = bind_text(database, query.get(), 2, object.key.value);
  if (!failed(error)) error = bind_uint64(database, query.get(), 3, object.generation);
  if (!failed(error)) error = bind_object_state(database, query.get(), 4, object_state::deleted);
  if (!failed(error)) error = bind_int64(database, query.get(), 5, part_number_marker);
  const std::size_t max_sqlite_limit = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
  const std::int64_t row_limit = max_parts >= max_sqlite_limit ? std::numeric_limits<std::int64_t>::max()
                                                               : static_cast<std::int64_t>(max_parts + 1);
  if (!failed(error)) error = bind_int64(database, query.get(), 6, row_limit);
  if (failed(error)) return error;

  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) break;
    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list object parts");
    if (result.parts.size() == max_parts) {
      result.is_truncated = true;
      break;
    }

    object_part_info part;
    part.part_number = static_cast<std::uint32_t>(sqlite3_column_int64(query.get(), 0));
    part.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    part.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    part.checksum.checksum_algorithm = checksum_algorithm_name{column_text(query.get(), 3)};
    part.checksum.value = column_text(query.get(), 4);
    part.checksum.type = static_cast<object_checksum_type>(sqlite3_column_int64(query.get(), 5));
    result.parts.push_back(std::move(part));
  }
  if (result.is_truncated && !result.parts.empty()) result.next_part_number_marker = result.parts.back().part_number;
  return {};
}

storage_error find_dedup_object_record(sqlite3* database, const checksum_algorithm_name& checksum_algorithm,
                                       std::string_view value, std::uint64_t content_length, indexed_object& result)
{
  statement query{database, R"sql(
    SELECT bucket.name, obj.key
    FROM objects AS obj
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    JOIN object_payloads AS payload ON payload.id = obj.payload_id
    WHERE obj.state = ?1
      AND obj.is_delete_marker = 0
      AND payload.dedup_algorithm = ?2
      AND payload.dedup_value = ?3
      AND payload.content_length = ?4
    LIMIT 1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare dedup lookup");

  storage_error error = bind_object_state(database, query.get(), 1, object_state::current);
  if (!failed(error)) error = bind_text(database, query.get(), 2, checksum_algorithm.value);
  if (!failed(error)) error = bind_text(database, query.get(), 3, value);
  if (!failed(error)) error = bind_uint64(database, query.get(), 4, content_length);
  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::object_not_found, "dedup object not found");
  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find dedup object");

  return find_object_record(database, bucket_name{column_text(query.get(), 0)}, object_key{column_text(query.get(), 1)},
                            result);
}

} // namespace extora::core::sqlite_detail
