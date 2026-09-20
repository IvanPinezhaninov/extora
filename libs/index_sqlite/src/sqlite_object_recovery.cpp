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
#include <utility>

#include "sqlite_common.h"
#include "sqlite_object_record_support.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

storage_error list_object_generation_records_for_recovery(sqlite3* database, std::vector<indexed_object>& result)
{
  result.clear();

  statement query{database, R"sql(
    SELECT
      obj.id,
      bucket.name,
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
      payload.dedup_algorithm,
      payload.dedup_value,
      obj.created_at,
      obj.modified_at,
      obj.generation
    FROM objects AS obj
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    JOIN object_payloads AS payload ON payload.id = obj.payload_id
    WHERE obj.state != ?1
      AND obj.state != ?2
      AND obj.is_delete_marker = 0
    ORDER BY
      bucket.name,
      obj.key,
      obj.generation
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare recovery object list");

  storage_error error = bind_object_state(database, query.get(), 1, object_state::deleted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::corrupted);
  if (failed(error)) return error;

  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) break;

    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list recovery objects");

    indexed_object object;
    object.bucket.value = column_text(query.get(), 1);
    object.key.value = column_text(query.get(), 2);
    object.payload.id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 3));
    object.metadata.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 4));
    object.payload.internal_checksum =
        object_checksum{checksum_algorithm_name{column_text(query.get(), 5)}, column_text(query.get(), 6)};

    object.metadata.content_type = column_optional_text(query.get(), 7);
    object.metadata.cache_control = column_optional_text(query.get(), 8);
    object.metadata.content_disposition = column_optional_text(query.get(), 9);
    object.metadata.content_encoding = column_optional_text(query.get(), 10);
    object.metadata.content_language = column_optional_text(query.get(), 11);
    object.metadata.expires_at = column_optional_time_point(query.get(), 12);
    object.metadata.checksum = column_optional_checksum(query.get(), 13, 14, 15);

    object.etag = column_text(query.get(), 16);
    if (!column_is_null(query.get(), 17))
      object.payload.dedup_algorithm = checksum_algorithm_name{column_text(query.get(), 17)};
    if (!column_is_null(query.get(), 18)) object.payload.dedup_value = column_text(query.get(), 18);
    object.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 19));
    object.modified_at = from_unix_ms(sqlite3_column_int64(query.get(), 20));
    object.generation = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 21));

    error = object_record_detail::load_payload_extents(database, static_cast<std::int64_t>(object.payload.id), object);
    if (failed(error)) return error;

    result.push_back(std::move(object));
  }

  return {};
}

storage_error mark_payload_corrupted_record(sqlite3* database, std::uint64_t payload_id)
{
  if (payload_id == 0 || payload_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return make_error(storage_error_code::index_failure, "invalid corrupted payload identifier");

  statement query{database, R"sql(
    UPDATE objects
    SET state = ?2
    WHERE payload_id = ?1
      AND state != ?3
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare corrupted payload quarantine");

  storage_error error = bind_uint64(database, query.get(), 1, payload_id);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::deleted);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to quarantine corrupted payload");
  return {};
}

} // namespace extora::core::sqlite_detail
