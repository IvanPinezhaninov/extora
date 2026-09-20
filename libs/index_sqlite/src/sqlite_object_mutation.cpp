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

#include <chrono>

#include "sqlite_bucket_records.h"
#include "sqlite_common.h"
#include "sqlite_extent_records.h"
#include "sqlite_object_record_support.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

storage_error delete_object_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                   const delete_object_options& options, const object_version_id& marker_version_id,
                                   delete_object_result& result, reclamation_estimate& reclaimable_delta)
{
  result = {};
  reclaimable_delta = {};
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  bucket_info bucket_state;
  error = find_bucket_record(database, bucket, bucket_state);
  if (failed(error)) return error;

  if (options.version_id.has_value()) {
    statement find_query{database, R"sql(
      SELECT obj.id, obj.is_delete_marker, obj.state,
        NOT EXISTS (
          SELECT 1
          FROM objects AS current_object
          WHERE current_object.bucket_id = obj.bucket_id
            AND current_object.key = obj.key
            AND current_object.state = ?6
        )
      FROM objects AS obj
      JOIN buckets AS bucket ON bucket.id = obj.bucket_id
      WHERE bucket.name = ?1
        AND obj.key = ?2
        AND ((?5 = 1 AND obj.version_id IS NULL) OR (?5 = 0 AND obj.version_id = ?3))
        AND obj.state != ?4
      ORDER BY obj.generation DESC
      LIMIT 1
      )sql"};
    if (!find_query.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare version deletion");
    error = bind_text(database, find_query.get(), 1, bucket.value);
    if (!failed(error)) error = bind_text(database, find_query.get(), 2, key.value);
    if (!failed(error)) error = bind_text(database, find_query.get(), 3, options.version_id->value);
    if (!failed(error)) error = bind_object_state(database, find_query.get(), 4, object_state::deleted);
    if (!failed(error))
      error = bind_int64(database, find_query.get(), 5, options.version_id->value == null_version_id ? 1 : 0);
    if (!failed(error)) error = bind_object_state(database, find_query.get(), 6, object_state::current);
    if (failed(error) || find_query.step() != SQLITE_ROW) {
      if (failed(error)) return error;
      return make_error(storage_error_code::object_not_found, "object version not found");
    }
    const std::int64_t object_id = sqlite3_column_int64(find_query.get(), 0);
    const std::int64_t object_state_value = sqlite3_column_int64(find_query.get(), 2);
    const bool was_latest =
        object_state_value == to_int(object_state::current) ||
        (object_state_value == to_int(object_state::corrupted) && sqlite3_column_int(find_query.get(), 3) != 0);
    result.version_id = *options.version_id;
    result.is_delete_marker = sqlite3_column_int(find_query.get(), 1) != 0;
    reclamation_estimate delta;
    error = mark_payload_extents_garbage(database, object_id, delta);
    if (!failed(error)) accumulate_reclamation_estimate(reclaimable_delta, delta);
    if (!failed(error)) {
      statement state_query{database, "UPDATE objects SET state = ?2 WHERE id = ?1 AND state != ?2"};
      if (!state_query.prepared())
        error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare version state");
      else {
        error = bind_int64(database, state_query.get(), 1, object_id);
        if (!failed(error)) error = bind_object_state(database, state_query.get(), 2, object_state::deleted);
        if (!failed(error) && (state_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1))
          error = make_sqlite_error(database, storage_error_code::index_failure, "failed to delete object version");
      }
    }
    if (!failed(error)) error = object_record_detail::detach_object_payload(database, object_id);
    if (!failed(error) && was_latest) {
      statement promote{database, R"sql(
        UPDATE objects
        SET state = ?3
        WHERE id = (
          SELECT obj.id
          FROM objects AS obj
          JOIN buckets AS bucket ON bucket.id = obj.bucket_id
          WHERE bucket.name = ?1
            AND obj.key = ?2
            AND obj.state = ?4
          ORDER BY obj.generation DESC
          LIMIT 1
        )
        )sql"};
      if (!promote.prepared())
        error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare version promotion");
      else {
        error = bind_text(database, promote.get(), 1, bucket.value);
        if (!failed(error)) error = bind_text(database, promote.get(), 2, key.value);
        if (!failed(error)) error = bind_object_state(database, promote.get(), 3, object_state::current);
        if (!failed(error)) error = bind_object_state(database, promote.get(), 4, object_state::superseded);
        if (!failed(error) && promote.step() != SQLITE_DONE)
          error = make_sqlite_error(database, storage_error_code::index_failure, "failed to promote object version");
      }
    }
    if (failed(error)) return error;
    return scope.commit();
  }

  std::int64_t object_id = 0;
  error = object_record_detail::find_current_object_id(database, bucket, key, object_id);
  if (failed(error)) {
    if (error.code == storage_error_code::object_not_found &&
        bucket_state.versioning != bucket_versioning_status::unversioned) {
      if (marker_version_id.value == null_version_id) {
        error = object_record_detail::delete_null_versions(database, bucket, key, 0, reclaimable_delta);
        if (failed(error)) return error;
      }
      statement marker{database, R"sql(
        INSERT INTO objects (
          bucket_id, payload_id, key, etag, version_id, is_delete_marker,
          created_at, modified_at, generation, state
        )
        VALUES (
          (SELECT id FROM buckets WHERE name = ?1), NULL, ?2, '', ?3, 1, ?4, ?4,
          (
            SELECT COALESCE(MAX(obj.generation), 0) + 1
            FROM objects AS obj
            JOIN buckets AS bucket ON bucket.id = obj.bucket_id
            WHERE bucket.name = ?1 AND obj.key = ?2
          ),
          ?5
        )
        )sql"};
      if (!marker.prepared()) {
        return make_sqlite_error(database, storage_error_code::index_failure,
                                 "failed to prepare missing-object delete marker");
      }
      const std::int64_t now = to_unix_ms(std::chrono::system_clock::now());
      error = bind_text(database, marker.get(), 1, bucket.value);
      if (!failed(error)) error = bind_text(database, marker.get(), 2, key.value);
      if (!failed(error) && marker_version_id.value == null_version_id) error = bind_null(database, marker.get(), 3);
      if (!failed(error) && marker_version_id.value != null_version_id)
        error = bind_text(database, marker.get(), 3, marker_version_id.value);
      if (!failed(error)) error = bind_int64(database, marker.get(), 4, now);
      if (!failed(error)) error = bind_object_state(database, marker.get(), 5, object_state::current);
      if (!failed(error) && marker.step() != SQLITE_DONE) {
        error = make_sqlite_error(database, storage_error_code::index_failure,
                                  "failed to insert missing-object delete marker");
      }
      if (failed(error)) return error;
      result.version_id = marker_version_id;
      result.is_delete_marker = true;
      return scope.commit();
    }
    if (error.code == storage_error_code::object_not_found &&
        bucket_state.versioning == bucket_versioning_status::unversioned) {
      result.version_id = object_version_id{null_version_id};
      result.is_delete_marker = false;
      return scope.commit();
    }
    return error;
  }

  if (bucket_state.versioning != bucket_versioning_status::unversioned) {
    bool replaces_current_null_version = false;
    if (marker_version_id.value == null_version_id) {
      indexed_object current_object;
      error = find_object_metadata_record(database, bucket, key, current_object);
      if (!failed(error)) replaces_current_null_version = current_object.version_id.value == null_version_id;
      if (!failed(error))
        error = object_record_detail::delete_null_versions(database, bucket, key, 0, reclaimable_delta);
    }
    if (!failed(error) && !replaces_current_null_version) {
      error = object_record_detail::make_current_object_state(database, object_id, object_state::superseded,
                                                              reclaimable_delta, true);
    }
    if (!failed(error)) {
      statement marker{database, R"sql(
        INSERT INTO objects (
          bucket_id, payload_id, key, etag, version_id, is_delete_marker,
          created_at, modified_at, generation, state
        )
        SELECT bucket_id, NULL, key, '', ?2, 1, ?3, ?3, generation + 1, ?4
        FROM objects
        WHERE id = ?1
        )sql"};
      if (!marker.prepared())
        error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare delete marker");
      else {
        const std::int64_t now = to_unix_ms(std::chrono::system_clock::now());
        error = bind_int64(database, marker.get(), 1, object_id);
        if (!failed(error) && marker_version_id.value == null_version_id) error = bind_null(database, marker.get(), 2);
        if (!failed(error) && marker_version_id.value != null_version_id)
          error = bind_text(database, marker.get(), 2, marker_version_id.value);
        if (!failed(error)) error = bind_int64(database, marker.get(), 3, now);
        if (!failed(error)) error = bind_object_state(database, marker.get(), 4, object_state::current);
        if (!failed(error) && marker.step() != SQLITE_DONE)
          error = make_sqlite_error(database, storage_error_code::index_failure, "failed to insert delete marker");
      }
    }
    result.version_id = marker_version_id;
    result.is_delete_marker = true;
  } else {
    error =
        object_record_detail::make_current_object_state(database, object_id, object_state::deleted, reclaimable_delta);
    result.version_id = object_version_id{null_version_id};
    result.is_delete_marker = false;
  }
  if (failed(error)) return error;
  return scope.commit();
}

} // namespace extora::core::sqlite_detail
