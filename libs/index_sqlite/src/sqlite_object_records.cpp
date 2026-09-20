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

#include "sqlite_bucket_records.h"
#include "sqlite_common.h"
#include "sqlite_extent_records.h"
#include "sqlite_multipart_records.h"
#include "sqlite_object_record_support.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error check_write_conditions(const indexed_object* object, const object_conditions& conditions)
{
  if (conditions.if_match_etag.has_value() && (object == nullptr || *conditions.if_match_etag != object->etag))
    return make_error(storage_error_code::precondition_failed, "If-Match condition failed");
  if (conditions.if_none_match_etag.has_value() && object != nullptr &&
      (*conditions.if_none_match_etag == etag_wildcard || *conditions.if_none_match_etag == object->etag))
    return make_error(storage_error_code::precondition_failed, "If-None-Match condition failed");
  if (conditions.if_modified_since.has_value() &&
      (object == nullptr || object->modified_at <= *conditions.if_modified_since))
    return make_error(storage_error_code::precondition_failed, "If-Modified-Since condition failed");
  if (conditions.if_unmodified_since.has_value() &&
      (object == nullptr || object->modified_at > *conditions.if_unmodified_since))
    return make_error(storage_error_code::precondition_failed, "If-Unmodified-Since condition failed");
  return {};
}

} // namespace

namespace object_record_detail {

storage_error find_current_object_id(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                     std::int64_t& object_id)
{
  statement query{database, R"sql(
    SELECT obj.id
    FROM buckets AS bucket
    LEFT JOIN objects AS obj ON obj.bucket_id = bucket.id
      AND obj.key = ?2
      AND (obj.state = ?3 OR obj.state = ?4)
    WHERE bucket.name = ?1
    ORDER BY obj.generation DESC
    LIMIT 1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare current object query");

  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_text(database, query.get(), 2, key.value);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::current);
  if (!failed(error)) error = bind_object_state(database, query.get(), 4, object_state::corrupted);
  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result == SQLITE_DONE) return make_error(storage_error_code::bucket_not_found, "bucket not found");

  if (step_result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find current object");

  if (column_is_null(query.get(), 0)) return make_error(storage_error_code::object_not_found, "object not found");

  object_id = sqlite3_column_int64(query.get(), 0);
  return {};
}

storage_error mark_current_object_state(sqlite3* database, std::int64_t object_id, object_state state,
                                        const char* failure_message)
{
  statement query{database, R"sql(
    UPDATE objects
    SET state = CASE
      WHEN state = ?5 AND ?4 = 0 THEN state
      ELSE ?2
    END
    WHERE id = ?1
      AND (state = ?3 OR state = ?5)
    )sql"};
  if (!query.prepared()) return make_sqlite_error(database, storage_error_code::index_failure, failure_message);

  storage_error error = bind_int64(database, query.get(), 1, object_id);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, state);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::current);
  if (!failed(error)) error = bind_int64(database, query.get(), 4, state == object_state::deleted ? 1 : 0);
  if (!failed(error)) error = bind_object_state(database, query.get(), 5, object_state::corrupted);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
    return make_sqlite_error(database, storage_error_code::index_failure, failure_message);

  return {};
}

storage_error make_current_object_state(sqlite3* database, std::int64_t object_id, object_state state,
                                        reclamation_estimate& reclaimable_delta, bool preserve_payload)
{
  if (!preserve_payload) {
    reclamation_estimate delta;
    const storage_error error = mark_payload_extents_garbage(database, object_id, delta);
    if (failed(error)) return error;
    accumulate_reclamation_estimate(reclaimable_delta, delta);
  }

  if (state == object_state::superseded)
    return mark_current_object_state(database, object_id, state, "failed to mark object superseded");

  return mark_current_object_state(database, object_id, state, "failed to mark object deleted");
}

storage_error detach_object_payload(sqlite3* database, std::int64_t object_id)
{
  statement query{database, "UPDATE objects SET payload_id = NULL WHERE id = ?1"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare payload detach");

  const storage_error error = bind_int64(database, query.get(), 1, object_id);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to detach payload");

  return {};
}

} // namespace object_record_detail

namespace {

storage_error insert_payload_extent(sqlite3* database, std::int64_t payload_id, std::size_t ordinal,
                                    std::int64_t physical_extent_id)
{
  statement query{database, R"sql(
    INSERT INTO payload_extents (
      payload_id,
      ordinal,
      physical_extent_id
    )
    VALUES (
      ?1,
      ?2,
      ?3
    )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare insert payload extent");

  storage_error error = bind_int64(database, query.get(), 1, payload_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, ordinal);
  if (!failed(error)) error = bind_int64(database, query.get(), 3, physical_extent_id);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to insert payload extent");

  return {};
}

storage_error insert_custom_metadata(sqlite3* database, std::int64_t object_id,
                                     const std::vector<metadata_entry>& metadata)
{
  statement query{database, R"sql(
    INSERT INTO object_metadata (object_id, ordinal, name, value)
    VALUES (?1, ?2, ?3, ?4)
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object metadata");
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    query.reset();
    storage_error error = bind_int64(database, query.get(), 1, object_id);
    if (!failed(error)) error = bind_uint64(database, query.get(), 2, index);
    if (!failed(error)) error = bind_text(database, query.get(), 3, metadata[index].name);
    if (!failed(error)) error = bind_text(database, query.get(), 4, metadata[index].value);
    if (failed(error)) return error;
    if (query.step() != SQLITE_DONE)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to insert object metadata");
  }
  return {};
}

storage_error insert_object_parts(sqlite3* database, std::int64_t object_id, const std::vector<object_part_info>& parts,
                                  std::uint64_t object_length)
{
  statement query{database, R"sql(
    INSERT INTO object_parts (
      object_id,
      part_number,
      offset,
      content_length,
      checksum_algorithm,
      checksum_value,
      checksum_type
    )
    VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object part");

  std::uint32_t previous_part_number = 0;
  std::uint64_t expected_offset = 0;
  for (const object_part_info& part : parts) {
    if (part.part_number == 0 || part.part_number <= previous_part_number || part.offset != expected_offset ||
        part.checksum.checksum_algorithm.value.empty() || part.checksum.value.empty() ||
        part.checksum.type != object_checksum_type::full_object)
      return make_error(storage_error_code::index_failure, "completed object part manifest is invalid");

    query.reset();
    storage_error error = bind_int64(database, query.get(), 1, object_id);
    if (!failed(error)) error = bind_int64(database, query.get(), 2, part.part_number);
    if (!failed(error)) error = bind_uint64(database, query.get(), 3, part.offset);
    if (!failed(error)) error = bind_uint64(database, query.get(), 4, part.content_length);
    if (!failed(error)) error = bind_text(database, query.get(), 5, part.checksum.checksum_algorithm.value);
    if (!failed(error)) error = bind_text(database, query.get(), 6, part.checksum.value);
    if (!failed(error)) error = bind_int64(database, query.get(), 7, static_cast<std::int64_t>(part.checksum.type));
    if (failed(error)) return error;
    if (query.step() != SQLITE_DONE)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to insert object part");

    previous_part_number = part.part_number;
    expected_offset += part.content_length;
  }
  if (!parts.empty() && expected_offset != object_length)
    return make_error(storage_error_code::index_failure, "completed object part manifest length does not match object");
  return {};
}

} // namespace

namespace object_record_detail {

storage_error load_custom_metadata(sqlite3* database, std::int64_t object_id, indexed_object& result)
{
  statement query{database, R"sql(
    SELECT name, value
    FROM object_metadata
    WHERE object_id = ?1
    ORDER BY ordinal
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object metadata lookup");
  storage_error error = bind_int64(database, query.get(), 1, object_id);
  if (failed(error)) return error;
  result.metadata.custom_metadata.clear();
  while (true) {
    const int step_result = query.step();
    if (step_result == SQLITE_DONE) return {};
    if (step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load object metadata");
    result.metadata.custom_metadata.push_back(metadata_entry{column_text(query.get(), 0), column_text(query.get(), 1)});
  }
}

} // namespace object_record_detail

namespace {

storage_error find_committed_extent_id(sqlite3* database, const physical_extent& extent, std::int64_t& extent_id)
{
  statement query{database, R"sql(
    SELECT id
    FROM physical_extents
    WHERE segment_id = ?1
      AND offset = ?2
      AND state = ?3
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare extent lookup");
  storage_error error = bind_uint64(database, query.get(), 1, extent.segment_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.offset);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 3, physical_extent_state::committed);
  if (failed(error)) return error;
  const int result = query.step();
  if (result == SQLITE_DONE) {
    extent_id = 0;
    return {};
  }
  if (result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find extent");
  extent_id = sqlite3_column_int64(query.get(), 0);
  return {};
}

} // namespace

namespace object_record_detail {

storage_error load_payload_extents(sqlite3* database, std::int64_t payload_id, indexed_object& result)
{
  statement extents_query{database, R"sql(
    SELECT
      physical_extent.segment_id,
      physical_extent.offset,
      physical_extent.length
    FROM payload_extents AS payload_extent
    JOIN physical_extents AS physical_extent ON physical_extent.id = payload_extent.physical_extent_id
    WHERE payload_extent.payload_id = ?1
    ORDER BY payload_extent.ordinal
    )sql"};
  if (!extents_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare find payload extents");

  storage_error error = bind_int64(database, extents_query.get(), 1, payload_id);
  if (failed(error)) return error;

  result.payload.extents.clear();

  while (true) {
    const int extent_step_result = extents_query.step();
    if (extent_step_result == SQLITE_DONE) break;

    if (extent_step_result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to find payload extents");

    physical_extent extent;
    extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 0));
    extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 1));
    extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 2));
    extent.reserved_length = extent.length;
    result.payload.extents.push_back(std::move(extent));
  }

  if (result.payload.extents.empty() &&
      (!result.metadata.content_length.has_value() || *result.metadata.content_length != 0))
    return make_error(storage_error_code::index_failure, "object payload has no committed extents");

  return {};
}

} // namespace object_record_detail

namespace {

storage_error insert_object_payload(sqlite3* database, const indexed_object& object, std::int64_t& payload_id)
{
  if (!object.metadata.content_length.has_value())
    return make_error(storage_error_code::index_failure, "object content length is missing");
  if (object.payload.internal_checksum.checksum_algorithm.value.empty() ||
      object.payload.internal_checksum.value.empty())
    return make_error(storage_error_code::index_failure, "object internal checksum is missing");

  statement query{database, R"sql(
    INSERT INTO object_payloads (
      content_length,
      internal_checksum_algorithm,
      internal_checksum_value,
      dedup_algorithm,
      dedup_value
    )
    VALUES (?1, ?2, ?3, ?4, ?5)
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object payload");

  storage_error error = bind_uint64(database, query.get(), 1, *object.metadata.content_length);
  if (!failed(error))
    error = bind_text(database, query.get(), 2, object.payload.internal_checksum.checksum_algorithm.value);
  if (!failed(error)) error = bind_text(database, query.get(), 3, object.payload.internal_checksum.value);
  if (!failed(error) && object.payload.dedup_algorithm.value.empty()) error = bind_null(database, query.get(), 4);
  if (!failed(error) && !object.payload.dedup_algorithm.value.empty())
    error = bind_text(database, query.get(), 4, object.payload.dedup_algorithm.value);
  if (!failed(error) && object.payload.dedup_value.empty()) error = bind_null(database, query.get(), 5);
  if (!failed(error) && !object.payload.dedup_value.empty())
    error = bind_text(database, query.get(), 5, object.payload.dedup_value);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to insert object payload");

  payload_id = sqlite3_last_insert_rowid(database);
  for (std::size_t ordinal = 0; ordinal < object.payload.extents.size(); ++ordinal) {
    const physical_extent& extent = object.payload.extents[ordinal];
    std::int64_t physical_extent_id = 0;
    error = find_committed_extent_id(database, extent, physical_extent_id);
    if (!failed(error) && physical_extent_id == 0)
      error = commit_reserved_extent_record(database, extent, physical_extent_id);
    if (failed(error)) return error;

    error = insert_payload_extent(database, payload_id, ordinal, physical_extent_id);
    if (failed(error)) return error;
  }

  return {};
}

} // namespace

namespace object_record_detail {

storage_error delete_null_versions(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                   std::uint64_t replacement_payload_id, reclamation_estimate& reclaimable_delta)
{
  statement find_query{database, R"sql(
    SELECT obj.id, obj.payload_id
    FROM objects AS obj
    JOIN buckets AS bucket ON bucket.id = obj.bucket_id
    WHERE bucket.name = ?1
      AND obj.key = ?2
      AND obj.version_id IS NULL
      AND obj.state != ?3
    )sql"};
  if (!find_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare null version lookup");

  storage_error error = bind_text(database, find_query.get(), 1, bucket.value);
  if (!failed(error)) error = bind_text(database, find_query.get(), 2, key.value);
  if (!failed(error)) error = bind_object_state(database, find_query.get(), 3, object_state::deleted);
  if (failed(error)) return error;

  std::vector<std::pair<std::int64_t, std::int64_t>> objects;
  while (true) {
    const int result = find_query.step();
    if (result == SQLITE_DONE) break;
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to find null version");
    objects.emplace_back(sqlite3_column_int64(find_query.get(), 0), sqlite3_column_int64(find_query.get(), 1));
  }

  statement delete_query{database, R"sql(
    UPDATE objects
    SET state = ?2, payload_id = NULL
    WHERE id = ?1
      AND state != ?3
    )sql"};
  if (!delete_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare null version deletion");

  for (const std::pair<std::int64_t, std::int64_t>& object : objects) {
    const std::int64_t object_id = object.first;
    const std::uint64_t payload_id = static_cast<std::uint64_t>(object.second);
    if (replacement_payload_id == 0 || payload_id != replacement_payload_id) {
      reclamation_estimate delta;
      error = mark_payload_extents_garbage(database, object_id, delta);
      if (failed(error)) return error;
      accumulate_reclamation_estimate(reclaimable_delta, delta);
    }

    delete_query.reset();
    error = bind_int64(database, delete_query.get(), 1, object_id);
    if (!failed(error)) error = bind_object_state(database, delete_query.get(), 2, object_state::deleted);
    if (!failed(error)) error = bind_object_state(database, delete_query.get(), 3, object_state::deleted);
    if (failed(error)) return error;
    if (delete_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete null version");
  }

  return {};
}

} // namespace object_record_detail

storage_error publish_object_record(sqlite3* database, const indexed_object& object,
                                    const object_conditions& conditions, reclamation_estimate& reclaimable_delta,
                                    const multipart_upload_id* completed_upload)
{
  reclaimable_delta = {};
  if (!object.metadata.content_length.has_value())
    return make_error(storage_error_code::index_failure, "object content length is missing");
  if (object.payload.extents.empty() && *object.metadata.content_length != 0)
    return make_error(storage_error_code::index_failure, "non-empty object has no extents");
  if (object.version_id.value.empty())
    return make_error(storage_error_code::index_failure, "object version identifier is empty");

  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  std::int64_t existing_object_id = 0;
  error = object_record_detail::find_current_object_id(database, object.bucket, object.key, existing_object_id);
  indexed_object existing_object;
  const bool existing_object_found = !failed(error);
  const indexed_object* existing_object_ptr = nullptr;
  if (existing_object_found) {
    error = find_object_metadata_record(database, object.bucket, object.key, existing_object);
    if (!failed(error) && !existing_object.is_delete_marker) existing_object_ptr = &existing_object;
  }
  if (error.code == storage_error_code::object_not_found) error = {};
  if (!failed(error)) error = check_write_conditions(existing_object_ptr, conditions);
  if (failed(error)) return error;

  bucket_info bucket;
  error = find_bucket_record(database, object.bucket, bucket);
  if (failed(error)) return error;

  const bool publishes_null_version = object.version_id.value == null_version_id;
  const bool replaces_current_null_version =
      publishes_null_version && existing_object_found && existing_object.version_id.value == null_version_id;
  if (publishes_null_version) {
    error = object_record_detail::delete_null_versions(database, object.bucket, object.key, object.payload.id,
                                                       reclaimable_delta);
    if (failed(error)) return error;
  }

  if (object.payload.id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return make_error(storage_error_code::index_failure, "object payload identifier is too large for SQLite");
  std::int64_t payload_id = static_cast<std::int64_t>(object.payload.id);
  if (payload_id == 0) {
    error = insert_object_payload(database, object, payload_id);
    if (failed(error)) return error;
  } else {
    statement payload_query{database, R"sql(
      SELECT 1
      FROM object_payloads
      WHERE id = ?1
      )sql"};
    if (!payload_query.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare payload lookup");
    error = bind_int64(database, payload_query.get(), 1, payload_id);
    if (failed(error) || payload_query.step() != SQLITE_ROW) {
      if (failed(error)) return error;
      return make_error(storage_error_code::index_failure, "object payload not found");
    }
  }

  statement query{database, R"sql(
    INSERT INTO objects (
      bucket_id,
      payload_id,
      key,
      content_type,
      cache_control,
      content_disposition,
      content_encoding,
      content_language,
      expires_at,
      checksum_algorithm,
      checksum_value,
      checksum_type,
      etag,
      version_id,
      is_delete_marker,
      created_at,
      modified_at,
      generation,
      state
    )
    VALUES (
      (
        SELECT id
        FROM buckets
        WHERE name = ?1
      ),
      ?2,
      ?3,
      ?4,
      ?5,
      ?6,
      ?7,
      ?8,
      ?9,
      ?10,
      ?11,
      ?12,
      ?13,
      ?14,
      ?15,
      ?16,
      ?17,
      (
        SELECT COALESCE(MAX(generation), 0) + 1
        FROM objects AS obj
        JOIN buckets AS bucket ON bucket.id = obj.bucket_id
        WHERE bucket.name = ?1
          AND obj.key = ?3
      ),
      ?18
    )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare publish object");

  error = bind_text(database, query.get(), 1, object.bucket.value);
  if (!failed(error)) error = bind_int64(database, query.get(), 2, payload_id);
  if (!failed(error)) error = bind_text(database, query.get(), 3, object.key.value);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 4, object.metadata.content_type);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 5, object.metadata.cache_control);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 6, object.metadata.content_disposition);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 7, object.metadata.content_encoding);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 8, object.metadata.content_language);
  if (!failed(error)) error = bind_optional_time_point(database, query.get(), 9, object.metadata.expires_at);
  if (!failed(error)) error = bind_optional_checksum(database, query.get(), 10, 11, object.metadata.checksum, 12);
  if (!failed(error)) error = bind_text(database, query.get(), 13, object.etag);
  if (!failed(error) && object.version_id.value == null_version_id) error = bind_null(database, query.get(), 14);
  if (!failed(error) && object.version_id.value != null_version_id)
    error = bind_text(database, query.get(), 14, object.version_id.value);
  if (!failed(error)) error = bind_int64(database, query.get(), 15, object.is_delete_marker ? 1 : 0);
  if (!failed(error)) error = bind_int64(database, query.get(), 16, to_unix_ms(object.created_at));
  if (!failed(error)) error = bind_int64(database, query.get(), 17, to_unix_ms(object.modified_at));
  if (!failed(error)) error = bind_object_state(database, query.get(), 18, object_state::current);

  if (failed(error)) return error;

  const int step_result = query.step();
  if (step_result != SQLITE_DONE) {
    if (step_result == SQLITE_CONSTRAINT || step_result == SQLITE_CONSTRAINT_FOREIGNKEY)
      return make_error(storage_error_code::bucket_not_found, "bucket not found");
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to publish object");
  }

  const std::int64_t object_id = sqlite3_last_insert_rowid(database);

  error = insert_custom_metadata(database, object_id, object.metadata.custom_metadata);
  if (failed(error)) return error;
  error = insert_object_parts(database, object_id, object.parts, *object.metadata.content_length);
  if (failed(error)) return error;

  if (existing_object_found && !replaces_current_null_version) {
    const bool preserve_existing = bucket.versioning != bucket_versioning_status::unversioned;
    error = object_record_detail::make_current_object_state(
        database, existing_object_id, preserve_existing ? object_state::superseded : object_state::deleted,
        reclaimable_delta, preserve_existing);
    if (!failed(error) && !preserve_existing)
      error = object_record_detail::detach_object_payload(database, existing_object_id);
    if (failed(error)) return error;
  }

  if (completed_upload != nullptr) {
    error = finish_multipart_upload_record(database, *completed_upload, reclaimable_delta);
    if (failed(error)) return error;
  }

  return scope.commit();
}

} // namespace extora::core::sqlite_detail
