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

#include "sqlite_multipart_records.h"

#include <cstdint>
#include <utility>

#include "sqlite_common.h"
#include "sqlite_extent_records.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error multipart_not_found()
{
  return make_error(storage_error_code::multipart_upload_not_found, "multipart upload not found");
}

storage_error verify_upload_target(sqlite3* database, const multipart_upload_id& upload_id, const bucket_name& bucket,
                                   const object_key& key)
{
  statement query{database, R"sql(
    SELECT 1
    FROM multipart_uploads AS upload
    JOIN buckets AS bucket ON bucket.id = upload.bucket_id
    WHERE upload.upload_id = ?1 AND bucket.name = ?2 AND upload.key = ?3
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart lookup");
  storage_error error = bind_text(database, query.get(), 1, upload_id.value);
  if (!failed(error)) error = bind_text(database, query.get(), 2, bucket.value);
  if (!failed(error)) error = bind_text(database, query.get(), 3, key.value);
  if (failed(error)) return error;
  const int result = query.step();
  if (result == SQLITE_DONE) return multipart_not_found();
  if (result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find multipart upload");
  return {};
}

storage_error abandon_part_extents(sqlite3* database, const multipart_upload_id& upload_id,
                                   const std::uint32_t* part_number, reclamation_estimate& reclaimable_delta)
{
  const char* candidates_sql = part_number == nullptr ? R"sql(
    SELECT
      COALESCE(SUM(length), 0),
      COUNT(*)
    FROM physical_extents
    WHERE state = ?2 AND id IN (
      SELECT physical_extent_id FROM multipart_part_extents
      WHERE upload_id = ?1
    )
    )sql"
                                                      : R"sql(
    SELECT
      COALESCE(SUM(length), 0),
      COUNT(*)
    FROM physical_extents
    WHERE state = ?3 AND id IN (
      SELECT physical_extent_id FROM multipart_part_extents
      WHERE upload_id = ?1 AND part_number = ?2
    )
    )sql";
  const char* sql = part_number == nullptr ? R"sql(
    UPDATE physical_extents
    SET state = ?2
    WHERE state = ?3 AND id IN (
      SELECT physical_extent_id FROM multipart_part_extents
      WHERE upload_id = ?1
    )
    )sql"
                                           : R"sql(
    UPDATE physical_extents
    SET state = ?3
    WHERE state = ?4 AND id IN (
      SELECT physical_extent_id FROM multipart_part_extents
      WHERE upload_id = ?1 AND part_number = ?2
    )
    )sql";

  statement candidates_query{database, candidates_sql};
  if (!candidates_query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare reclaimable multipart extent count");
  }
  storage_error error = bind_text(database, candidates_query.get(), 1, upload_id.value);
  int state_index = 2;
  if (part_number != nullptr) {
    if (!failed(error))
      error = bind_int64(database, candidates_query.get(), 2, static_cast<std::int64_t>(*part_number));
    state_index = 3;
  }
  if (!failed(error))
    error = bind_physical_extent_state(database, candidates_query.get(), state_index, physical_extent_state::reserved);
  if (failed(error)) return error;
  if (candidates_query.step() != SQLITE_ROW) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to count reclaimable multipart extents");
  }

  reclamation_estimate delta;
  delta.reclaimable_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(candidates_query.get(), 0));
  delta.reclaimable_extent_count = static_cast<std::uint64_t>(sqlite3_column_int64(candidates_query.get(), 1));

  statement query{database, sql};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart extent cleanup");
  error = bind_text(database, query.get(), 1, upload_id.value);
  state_index = 2;
  if (part_number != nullptr) {
    if (!failed(error)) error = bind_int64(database, query.get(), 2, static_cast<std::int64_t>(*part_number));
    state_index = 3;
  }
  if (!failed(error))
    error = bind_physical_extent_state(database, query.get(), state_index, physical_extent_state::abandoned);
  if (!failed(error))
    error = bind_physical_extent_state(database, query.get(), state_index + 1, physical_extent_state::reserved);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to abandon multipart extents");
  accumulate_reclamation_estimate(reclaimable_delta, delta);
  return {};
}

storage_error delete_upload(sqlite3* database, const multipart_upload_id& upload_id)
{
  statement query{database, "DELETE FROM multipart_uploads WHERE upload_id = ?1"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart deletion");
  storage_error error = bind_text(database, query.get(), 1, upload_id.value);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete multipart upload");
  if (sqlite3_changes(database) != 1) return multipart_not_found();
  return {};
}

storage_error load_upload_metadata(sqlite3* database, indexed_multipart_upload& upload)
{
  statement query{database, R"sql(
    SELECT name, value FROM multipart_upload_metadata
    WHERE upload_id = ?1 ORDER BY ordinal
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart metadata");
  storage_error error = bind_text(database, query.get(), 1, upload.upload_id.value);
  if (failed(error)) return error;
  while (true) {
    const int result = query.step();
    if (result == SQLITE_DONE) return {};
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load multipart metadata");
    upload.metadata.custom_metadata.push_back(metadata_entry{column_text(query.get(), 0), column_text(query.get(), 1)});
  }
}

storage_error load_part_extents(sqlite3* database, const multipart_upload_id& upload_id, indexed_multipart_part& part)
{
  statement query{database, R"sql(
    SELECT physical.segment_id, physical.offset, physical.length
    FROM multipart_part_extents AS extent
    JOIN physical_extents AS physical ON physical.id = extent.physical_extent_id
    WHERE extent.upload_id = ?1 AND extent.part_number = ?2
    ORDER BY extent.ordinal
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart extents");
  storage_error error = bind_text(database, query.get(), 1, upload_id.value);
  if (!failed(error)) error = bind_int64(database, query.get(), 2, part.info.part_number);
  if (failed(error)) return error;
  while (true) {
    const int result = query.step();
    if (result == SQLITE_DONE) return {};
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load multipart extents");
    physical_extent extent;
    extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
    extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    extent.reserved_length = extent.length;
    part.extents.push_back(std::move(extent));
  }
}

storage_error load_parts(sqlite3* database, indexed_multipart_upload& upload)
{
  statement query{database, R"sql(
    SELECT part_number, etag, content_length,
      internal_checksum_algorithm, internal_checksum_value,
      checksum_algorithm, checksum_value, created_at
    FROM multipart_parts WHERE upload_id = ?1 ORDER BY part_number
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart parts");
  storage_error error = bind_text(database, query.get(), 1, upload.upload_id.value);
  if (failed(error)) return error;
  while (true) {
    const int result = query.step();
    if (result == SQLITE_DONE) break;
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load multipart parts");
    indexed_multipart_part part;
    part.info.part_number = static_cast<std::uint32_t>(sqlite3_column_int64(query.get(), 0));
    part.info.etag = column_text(query.get(), 1);
    part.info.content_length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    part.internal_checksum =
        object_checksum{checksum_algorithm_name{column_text(query.get(), 3)}, column_text(query.get(), 4)};
    part.info.checksum = column_optional_checksum(query.get(), 5, 6);
    part.info.created_at = from_unix_ms(sqlite3_column_int64(query.get(), 7));
    upload.parts.push_back(std::move(part));
  }
  for (indexed_multipart_part& part : upload.parts) {
    error = load_part_extents(database, upload.upload_id, part);
    if (failed(error)) return error;
  }
  return {};
}

} // namespace

storage_error create_multipart_upload_record(sqlite3* database, const indexed_multipart_upload& upload)
{
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;
  statement query{database, R"sql(
    INSERT INTO multipart_uploads (
      upload_id, bucket_id, key, content_type, cache_control, content_disposition,
      content_encoding, content_language, expires_at, checksum_algorithm, checksum_type,
      deduplication, initiated_at
    ) VALUES (?1, (SELECT id FROM buckets WHERE name = ?2), ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)
    )sql"};
  if (!query.prepared())
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart creation");
  if (!failed(error)) error = bind_text(database, query.get(), 1, upload.upload_id.value);
  if (!failed(error)) error = bind_text(database, query.get(), 2, upload.bucket.value);
  if (!failed(error)) error = bind_text(database, query.get(), 3, upload.key.value);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 4, upload.metadata.content_type);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 5, upload.metadata.cache_control);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 6, upload.metadata.content_disposition);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 7, upload.metadata.content_encoding);
  if (!failed(error)) error = bind_optional_text(database, query.get(), 8, upload.metadata.content_language);
  if (!failed(error)) error = bind_optional_time_point(database, query.get(), 9, upload.metadata.expires_at);
  if (!failed(error)) error = bind_text(database, query.get(), 10, upload.checksum_algorithm.value);
  if (!failed(error)) error = bind_int64(database, query.get(), 11, static_cast<std::int64_t>(upload.checksum_type));
  if (!failed(error)) error = bind_int64(database, query.get(), 12, static_cast<std::int64_t>(upload.dedup));
  if (!failed(error)) error = bind_int64(database, query.get(), 13, to_unix_ms(upload.initiated_at));
  if (!failed(error)) {
    const int result = query.step();
    const int extended_result = sqlite3_extended_errcode(database);
    if (result == SQLITE_CONSTRAINT &&
        (extended_result == SQLITE_CONSTRAINT_FOREIGNKEY || extended_result == SQLITE_CONSTRAINT_NOTNULL))
      error = make_error(storage_error_code::bucket_not_found, "bucket not found");
    else if (result != SQLITE_DONE)
      error = make_sqlite_error(database, storage_error_code::index_failure, "failed to create multipart upload");
  }

  statement metadata{database, R"sql(
    INSERT INTO multipart_upload_metadata (upload_id, ordinal, name, value) VALUES (?1, ?2, ?3, ?4)
    )sql"};
  if (!failed(error) && !metadata.prepared())
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart metadata");
  for (std::size_t index = 0; !failed(error) && index < upload.metadata.custom_metadata.size(); ++index) {
    metadata.reset();
    error = bind_text(database, metadata.get(), 1, upload.upload_id.value);
    if (!failed(error)) error = bind_uint64(database, metadata.get(), 2, index);
    if (!failed(error)) error = bind_text(database, metadata.get(), 3, upload.metadata.custom_metadata[index].name);
    if (!failed(error)) error = bind_text(database, metadata.get(), 4, upload.metadata.custom_metadata[index].value);
    if (!failed(error) && metadata.step() != SQLITE_DONE)
      error = make_sqlite_error(database, storage_error_code::index_failure, "failed to store multipart metadata");
  }
  if (failed(error)) return error;
  return scope.commit();
}

storage_error find_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                           indexed_multipart_upload& result)
{
  result = {};
  statement query{database, R"sql(
    SELECT bucket.name, upload.key, upload.content_type, upload.cache_control,
      upload.content_disposition, upload.content_encoding, upload.content_language,
      upload.expires_at, upload.checksum_algorithm, upload.checksum_type,
      upload.deduplication, upload.initiated_at
    FROM multipart_uploads AS upload JOIN buckets AS bucket ON bucket.id = upload.bucket_id
    WHERE upload.upload_id = ?1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart upload");
  storage_error error = bind_text(database, query.get(), 1, upload_id.value);
  if (failed(error)) return error;
  const int step = query.step();
  if (step == SQLITE_DONE) return multipart_not_found();
  if (step != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to load multipart upload");
  result.upload_id = upload_id;
  result.bucket.value = column_text(query.get(), 0);
  result.key.value = column_text(query.get(), 1);
  result.metadata.content_type = column_optional_text(query.get(), 2);
  result.metadata.cache_control = column_optional_text(query.get(), 3);
  result.metadata.content_disposition = column_optional_text(query.get(), 4);
  result.metadata.content_encoding = column_optional_text(query.get(), 5);
  result.metadata.content_language = column_optional_text(query.get(), 6);
  result.metadata.expires_at = column_optional_time_point(query.get(), 7);
  result.checksum_algorithm = checksum_algorithm_name{column_text(query.get(), 8)};
  const std::int64_t checksum_type = sqlite3_column_int64(query.get(), 9);
  if (checksum_type < static_cast<std::int64_t>(object_checksum_type::full_object) ||
      checksum_type > static_cast<std::int64_t>(object_checksum_type::composite))
    return make_error(storage_error_code::index_failure, "invalid multipart checksum type");
  result.checksum_type = static_cast<object_checksum_type>(checksum_type);
  const std::int64_t dedup = sqlite3_column_int64(query.get(), 10);
  if (dedup < static_cast<std::int64_t>(dedup_mode::enabled) || dedup > static_cast<std::int64_t>(dedup_mode::disabled))
    return make_error(storage_error_code::index_failure, "invalid multipart deduplication mode");
  result.dedup = static_cast<dedup_mode>(dedup);
  result.initiated_at = from_unix_ms(sqlite3_column_int64(query.get(), 11));
  error = load_upload_metadata(database, result);
  if (!failed(error)) error = load_parts(database, result);
  return error;
}

storage_error store_multipart_part_record(sqlite3* database, const multipart_upload_id& upload_id,
                                          const bucket_name& bucket, const object_key& key,
                                          const indexed_multipart_part& part, reclamation_estimate& reclaimable_delta)
{
  reclaimable_delta = {};
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;
  if (part.internal_checksum.checksum_algorithm.value.empty() || part.internal_checksum.value.empty())
    return make_error(storage_error_code::index_failure, "multipart part internal checksum is missing");
  error = verify_upload_target(database, upload_id, bucket, key);
  if (!failed(error)) error = abandon_part_extents(database, upload_id, &part.info.part_number, reclaimable_delta);
  statement remove{database, "DELETE FROM multipart_parts WHERE upload_id = ?1 AND part_number = ?2"};
  if (!failed(error) && !remove.prepared())
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart replacement");
  if (!failed(error)) error = bind_text(database, remove.get(), 1, upload_id.value);
  if (!failed(error)) error = bind_int64(database, remove.get(), 2, part.info.part_number);
  if (!failed(error) && remove.step() != SQLITE_DONE)
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to replace multipart part");

  statement insert{database, R"sql(
    INSERT INTO multipart_parts (upload_id, part_number, etag, content_length,
      internal_checksum_algorithm, internal_checksum_value,
      checksum_algorithm, checksum_value, created_at)
    VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
    )sql"};
  if (!failed(error) && !insert.prepared())
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart part");
  if (!failed(error)) error = bind_text(database, insert.get(), 1, upload_id.value);
  if (!failed(error)) error = bind_int64(database, insert.get(), 2, part.info.part_number);
  if (!failed(error)) error = bind_text(database, insert.get(), 3, part.info.etag);
  if (!failed(error)) error = bind_uint64(database, insert.get(), 4, part.info.content_length);
  if (!failed(error)) error = bind_text(database, insert.get(), 5, part.internal_checksum.checksum_algorithm.value);
  if (!failed(error)) error = bind_text(database, insert.get(), 6, part.internal_checksum.value);
  if (!failed(error)) error = bind_optional_checksum(database, insert.get(), 7, 8, part.info.checksum);
  if (!failed(error)) error = bind_int64(database, insert.get(), 9, to_unix_ms(part.info.created_at));
  if (!failed(error) && insert.step() != SQLITE_DONE)
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to store multipart part");

  statement extent_insert{database, R"sql(
    INSERT INTO multipart_part_extents (upload_id, part_number, ordinal, physical_extent_id)
    VALUES (?1, ?2, ?3, ?4)
    )sql"};
  if (!failed(error) && !extent_insert.prepared())
    error = make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart extent");
  for (std::size_t ordinal = 0; ordinal < part.extents.size(); ++ordinal) {
    const physical_extent& extent = part.extents[ordinal];
    if (failed(error)) break;
    std::int64_t physical_extent_id = 0;
    error = trim_reserved_extent_record(database, extent, physical_extent_id);
    if (failed(error)) break;

    extent_insert.reset();
    error = bind_text(database, extent_insert.get(), 1, upload_id.value);
    if (!failed(error)) error = bind_int64(database, extent_insert.get(), 2, part.info.part_number);
    if (!failed(error)) error = bind_uint64(database, extent_insert.get(), 3, ordinal);
    if (!failed(error)) error = bind_int64(database, extent_insert.get(), 4, physical_extent_id);
    if (!failed(error) && extent_insert.step() != SQLITE_DONE)
      error = make_sqlite_error(database, storage_error_code::index_failure, "failed to store multipart extent");
  }
  if (failed(error)) return error;
  return scope.commit();
}

storage_error finish_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                             reclamation_estimate& reclaimable_delta)
{
  storage_error error = abandon_part_extents(database, upload_id, nullptr, reclaimable_delta);
  if (!failed(error)) error = delete_upload(database, upload_id);
  return error;
}

storage_error abort_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                            const bucket_name& bucket, const object_key& key,
                                            reclamation_estimate& reclaimable_delta)
{
  reclaimable_delta = {};
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;
  error = verify_upload_target(database, upload_id, bucket, key);
  if (!failed(error)) error = finish_multipart_upload_record(database, upload_id, reclaimable_delta);
  if (failed(error)) return error;
  return scope.commit();
}

storage_error list_multipart_upload_records(sqlite3* database, const bucket_name& bucket,
                                            std::vector<indexed_multipart_upload>& result)
{
  result.clear();
  statement query{database, R"sql(
    SELECT upload.upload_id FROM multipart_uploads AS upload
    JOIN buckets AS bucket ON bucket.id = upload.bucket_id
    WHERE bucket.name = ?1 ORDER BY upload.key, upload.initiated_at, upload.upload_id
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare multipart list");
  storage_error error = bind_text(database, query.get(), 1, bucket.value);
  if (failed(error)) return error;
  std::vector<multipart_upload_id> ids;
  while (true) {
    const int step = query.step();
    if (step == SQLITE_DONE) break;
    if (step != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to list multipart uploads");
    ids.push_back(multipart_upload_id{column_text(query.get(), 0)});
  }
  for (const multipart_upload_id& id : ids) {
    indexed_multipart_upload upload;
    error = find_multipart_upload_record(database, id, upload);
    if (failed(error)) return error;
    result.push_back(std::move(upload));
  }
  if (result.empty()) {
    statement exists{database, "SELECT 1 FROM buckets WHERE name = ?1"};
    if (!exists.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare bucket lookup");
    error = bind_text(database, exists.get(), 1, bucket.value);
    if (failed(error)) return error;
    if (exists.step() != SQLITE_ROW) return make_error(storage_error_code::bucket_not_found, "bucket not found");
  }
  return {};
}

} // namespace extora::core::sqlite_detail
