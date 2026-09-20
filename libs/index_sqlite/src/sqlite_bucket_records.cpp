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

#include "sqlite_bucket_records.h"

#include <chrono>
#include <limits>
#include <utility>

#include "sqlite_common.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error decode_bucket_versioning_status(int value, bucket_versioning_status& result)
{
  switch (value) {
  case 0:
    result = bucket_versioning_status::unversioned;
    return {};
  case 1:
    result = bucket_versioning_status::enabled;
    return {};
  case 2:
    result = bucket_versioning_status::suspended;
    return {};
  default:
    return make_error(storage_error_code::index_failure, "invalid bucket versioning state");
  }
}

} // namespace

storage_error create_bucket_record(sqlite3* database, const bucket_name& bucket)
{
  statement query{database, R"sql(
    INSERT INTO buckets (
      name,
      created_at
    )
    VALUES (
      ?1,
      ?2
    )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare create bucket");

  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (failed(error)) return error;

  error = bind_int64(database, query.get(), 2, to_unix_ms(std::chrono::system_clock::now()));
  if (failed(error)) return error;

  const int result = query.step();
  if (result == SQLITE_DONE) return {};

  if (result == SQLITE_CONSTRAINT || result == SQLITE_CONSTRAINT_PRIMARYKEY)
    return make_error(storage_error_code::bucket_already_exists, "bucket already exists");

  return make_sqlite_error(database, storage_error_code::index_failure, "failed to create bucket");
}

storage_error delete_bucket_record(sqlite3* database, const bucket_name& bucket)
{
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  statement bucket_query{database, "SELECT id FROM buckets WHERE name = ?1"};
  if (!bucket_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare delete bucket lookup");
  error = bind_text(database, bucket_query.get(), 1, bucket.value);
  if (failed(error)) return error;
  const int step_result = bucket_query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::bucket_not_found, "bucket not found");
  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find bucket for deletion");
  const std::int64_t bucket_id = sqlite3_column_int64(bucket_query.get(), 0);

  statement contents_query{database, R"sql(
    SELECT 1 FROM objects WHERE bucket_id = ?1 AND state != ?2
    UNION ALL
    SELECT 1 FROM multipart_uploads WHERE bucket_id = ?1
    LIMIT 1
    )sql"};
  if (!contents_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket contents lookup");
  error = bind_int64(database, contents_query.get(), 1, bucket_id);
  if (!failed(error)) error = bind_object_state(database, contents_query.get(), 2, object_state::deleted);
  if (failed(error)) return error;
  if (contents_query.step() == SQLITE_ROW)
    return make_error(storage_error_code::bucket_not_empty, "bucket is not empty");

  statement objects_query{database, "DELETE FROM objects WHERE bucket_id = ?1"};
  if (!objects_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket object deletion");
  error = bind_int64(database, objects_query.get(), 1, bucket_id);
  if (failed(error) || objects_query.step() != SQLITE_DONE) {
    return failed(error)
               ? error
               : make_sqlite_error(database, storage_error_code::index_failure, "failed to delete bucket objects");
  }

  statement delete_query{database, "DELETE FROM buckets WHERE id = ?1"};
  if (!delete_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket deletion");
  error = bind_int64(database, delete_query.get(), 1, bucket_id);
  if (failed(error) || delete_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1) {
    return failed(error) ? error
                         : make_sqlite_error(database, storage_error_code::index_failure, "failed to delete bucket");
  }

  return scope.commit();
}

storage_error find_bucket_record(sqlite3* database, const bucket_name& bucket, bucket_info& result)
{
  statement query{database, R"sql(
    SELECT
      id,
      name,
      created_at,
      versioning_enabled
    FROM buckets
    WHERE name = ?1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare find bucket");

  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::bucket_not_found, "bucket not found");

  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find bucket");

  result.name.value = column_text(query.get(), 1);
  result.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 2));
  return decode_bucket_versioning_status(sqlite3_column_int(query.get(), 3), result.versioning);
}

storage_error list_bucket_records(sqlite3* database, const list_buckets_options& options, bucket_list& result)
{
  result = {};

  statement query{database, R"sql(
    SELECT
      name,
      created_at,
      versioning_enabled
    FROM buckets
    WHERE (?1 = '' OR name >= ?1)
      AND (?2 = '' OR name > ?2)
    ORDER BY name
    LIMIT ?3
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare list buckets");

  const std::size_t max_sqlite_limit = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
  const std::int64_t row_limit = options.max_buckets >= max_sqlite_limit
                                     ? std::numeric_limits<std::int64_t>::max()
                                     : static_cast<std::int64_t>(options.max_buckets + 1);
  storage_error error = bind_text(database, query.get(), 1, options.prefix);
  if (!failed(error)) error = bind_text(database, query.get(), 2, options.continuation_token);
  if (!failed(error)) error = bind_int64(database, query.get(), 3, row_limit);
  if (failed(error)) return error;

  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) return {};

    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list buckets");

    bucket_info info;
    info.name.value = column_text(query.get(), 0);
    if (!options.prefix.empty() && info.name.value.compare(0, options.prefix.size(), options.prefix) != 0) break;
    if (result.buckets.size() == options.max_buckets) {
      result.next_continuation_token = result.buckets.back().name.value;
      break;
    }
    info.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 1));

    error = decode_bucket_versioning_status(sqlite3_column_int(query.get(), 2), info.versioning);
    if (failed(error)) return error;

    result.buckets.push_back(std::move(info));
  }
  return {};
}

storage_error read_bucket_usage(sqlite3* database, const bucket_name& bucket, bucket_usage& usage)
{
  usage = {};

  statement query{database, R"sql(
    WITH target_bucket AS (
      SELECT id
      FROM buckets
      WHERE name = ?1
    ),
    object_usage AS (
      SELECT
        COALESCE(SUM(CASE WHEN object.state = ?2 THEN payload.content_length ELSE 0 END), 0) AS current_bytes,
        COALESCE(SUM(CASE WHEN object.state = ?3 THEN payload.content_length ELSE 0 END), 0) AS noncurrent_bytes,
        COALESCE(SUM(CASE WHEN object.state = ?2 THEN 1 ELSE 0 END), 0) AS current_count,
        COALESCE(SUM(CASE WHEN object.state = ?3 THEN 1 ELSE 0 END), 0) AS noncurrent_count
      FROM objects AS object
      INNER JOIN object_payloads AS payload ON payload.id = object.payload_id
      WHERE object.bucket_id = (SELECT id FROM target_bucket)
        AND object.is_delete_marker = 0
    ),
    multipart_usage AS (
      SELECT
        COALESCE(SUM(part.content_length), 0) AS multipart_bytes,
        COUNT(*) AS multipart_count
      FROM multipart_uploads AS upload
      INNER JOIN multipart_parts AS part ON part.upload_id = upload.upload_id
      WHERE upload.bucket_id = (SELECT id FROM target_bucket)
    )
    SELECT
      object_usage.current_bytes,
      object_usage.noncurrent_bytes,
      multipart_usage.multipart_bytes,
      object_usage.current_count,
      object_usage.noncurrent_count,
      multipart_usage.multipart_count
    FROM target_bucket
    CROSS JOIN object_usage
    CROSS JOIN multipart_usage
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket usage");

  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::current);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::superseded);
  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::bucket_not_found, "bucket not found");
  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read bucket usage");

  for (int column = 0; column < 6; ++column) {
    if (sqlite3_column_int64(query.get(), column) < 0)
      return make_error(storage_error_code::index_failure, "bucket usage exceeds the supported range");
  }

  bucket_usage result;
  result.current_object_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
  result.noncurrent_version_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
  result.multipart_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
  result.current_object_count = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 3));
  result.noncurrent_version_count = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 4));
  result.multipart_part_count = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 5));
  usage = result;
  return {};
}

storage_error set_bucket_versioning_record(sqlite3* database, const bucket_name& bucket,
                                           bucket_versioning_status status)
{
  bucket_info current;
  storage_error error = find_bucket_record(database, bucket, current);
  if (failed(error)) return error;
  if (status == bucket_versioning_status::unversioned && current.versioning != bucket_versioning_status::unversioned)
    return make_error(storage_error_code::precondition_failed, "cannot return a versioned bucket to unversioned state");

  statement query{database, R"sql(
    UPDATE buckets
    SET versioning_enabled = ?2
    WHERE name = ?1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket versioning");
  error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_int64(database, query.get(), 2, static_cast<std::int64_t>(status));
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to update bucket versioning");
  if (sqlite3_changes(database) != 1) return make_error(storage_error_code::bucket_not_found, "bucket not found");
  return {};
}

} // namespace extora::core::sqlite_detail
