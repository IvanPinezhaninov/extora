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

#include "sqlite_normalization.h"

#include <utility>
#include <vector>

#include "sqlite_common.h"
#include "sqlite_extent_records.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error ensure_protected_extents_table(sqlite3* database)
{
  return execute(database, R"sql(
    CREATE TEMP TABLE IF NOT EXISTS cleanup_protected_extents (
      segment_id INTEGER NOT NULL,
      offset INTEGER NOT NULL,
      PRIMARY KEY (segment_id, offset)
    )
    )sql");
}

storage_error replace_protected_extents(sqlite3* database, const std::vector<physical_extent>& protected_extents)
{
  storage_error error = ensure_protected_extents_table(database);
  if (!failed(error)) error = execute(database, "DELETE FROM cleanup_protected_extents");
  if (failed(error)) return error;

  statement query{database, R"sql(
    INSERT INTO cleanup_protected_extents (
      segment_id,
      offset
    )
    VALUES (
      ?1,
      ?2
    )
    )sql"};
  if (!query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare protected extent registration");
  }

  for (const physical_extent& extent : protected_extents) {
    query.reset();
    error = bind_uint64(database, query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.offset);
    if (failed(error)) return error;
    if (query.step() != SQLITE_DONE)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to register protected extent");
  }

  return {};
}

storage_error abandon_incomplete_extents(sqlite3* database)
{
  statement query{database, R"sql(
    UPDATE physical_extents
    SET
      state = ?1,
      operation_id = NULL,
      operation_ordinal = NULL
    WHERE state = ?2
      AND NOT EXISTS (
        SELECT 1
        FROM multipart_part_extents
        WHERE multipart_part_extents.physical_extent_id = physical_extents.id
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reserved extent recovery");

  storage_error error = bind_physical_extent_state(database, query.get(), 1, physical_extent_state::abandoned);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::reserved);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to recover reserved extents");

  return {};
}

storage_error quarantine_invalid_current_objects(sqlite3* database)
{
  statement query{database, R"sql(
    UPDATE objects
    SET state = ?1
    WHERE state = ?2
      AND is_delete_marker = 0
      AND (
        NOT EXISTS (
          SELECT 1
          FROM object_payloads AS payload
          WHERE payload.id = objects.payload_id
            AND (
              payload.content_length = 0
              OR EXISTS (
                SELECT 1
                FROM payload_extents
                WHERE payload_extents.payload_id = payload.id
              )
            )
        )
        OR EXISTS (
          SELECT 1
          FROM payload_extents AS payload_extent
          LEFT JOIN physical_extents AS physical_extent
            ON physical_extent.id = payload_extent.physical_extent_id
          WHERE payload_extent.payload_id = objects.payload_id
            AND (
              physical_extent.id IS NULL
              OR physical_extent.state <> ?3
            )
        )
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare object recovery");

  storage_error error = bind_object_state(database, query.get(), 1, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::current);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 3, physical_extent_state::committed);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to recover object records");

  statement shared_payloads{database, R"sql(
    UPDATE objects
    SET state = ?1
    WHERE state != ?2
      AND payload_id IN (
        SELECT payload_id
        FROM objects
        WHERE state = ?1
          AND payload_id IS NOT NULL
      )
    )sql"};
  if (!shared_payloads.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare shared corrupted payload quarantine");
  error = bind_object_state(database, shared_payloads.get(), 1, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, shared_payloads.get(), 2, object_state::deleted);
  if (failed(error)) return error;
  if (shared_payloads.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to quarantine shared corrupted payloads");

  return {};
}

storage_error promote_recoverable_versions(sqlite3* database)
{
  statement query{database, R"sql(
    UPDATE objects AS candidate
    SET state = ?1
    WHERE candidate.state = ?2
      AND EXISTS (
        SELECT 1
        FROM buckets
        WHERE buckets.id = candidate.bucket_id
          AND buckets.versioning_enabled != 0
      )
      AND NOT EXISTS (
        SELECT 1
        FROM objects AS current_object
        WHERE current_object.bucket_id = candidate.bucket_id
          AND current_object.key = candidate.key
          AND current_object.state = ?1
      )
      AND NOT EXISTS (
        SELECT 1
        FROM objects AS corrupted_object
        WHERE corrupted_object.bucket_id = candidate.bucket_id
          AND corrupted_object.key = candidate.key
          AND corrupted_object.state = ?3
      )
      AND candidate.id = (
        SELECT previous.id
        FROM objects AS previous
        WHERE previous.bucket_id = candidate.bucket_id
          AND previous.key = candidate.key
          AND previous.state = ?2
        ORDER BY previous.generation DESC
        LIMIT 1
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare version recovery");
  storage_error error = bind_object_state(database, query.get(), 1, object_state::current);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::superseded);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::corrupted);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to recover previous versions");
  return {};
}

storage_error read_reclaimed_extents(sqlite3* database, reclamation_estimate& estimate)
{
  estimate = {};
  statement query{database, R"sql(
    SELECT
      COALESCE(SUM(length), 0),
      COUNT(*)
    FROM physical_extents
    WHERE (state = ?1
      OR state = ?2)
      AND NOT EXISTS (
        SELECT 1
        FROM cleanup_protected_extents
        WHERE cleanup_protected_extents.segment_id = physical_extents.segment_id
          AND cleanup_protected_extents.offset = physical_extents.offset
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reclaimed extent count");

  storage_error error = bind_physical_extent_state(database, query.get(), 1, physical_extent_state::garbage);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::abandoned);
  if (failed(error)) return error;
  if (query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to count reclaimed extents");

  estimate.reclaimable_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
  estimate.reclaimable_extent_count = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
  return {};
}

storage_error normalize_reusable_extents(sqlite3* database, reclamation_estimate* reclaimed = nullptr)
{
  if (reclaimed != nullptr) {
    const storage_error error = read_reclaimed_extents(database, *reclaimed);
    if (failed(error)) return error;
  }

  statement unlink_query{database, R"sql(
    DELETE FROM payload_extents
    WHERE physical_extent_id IN (
      SELECT id
      FROM physical_extents
      WHERE (state = ?1
        OR state = ?2)
        AND NOT EXISTS (
          SELECT 1
          FROM cleanup_protected_extents
          WHERE cleanup_protected_extents.segment_id = physical_extents.segment_id
            AND cleanup_protected_extents.offset = physical_extents.offset
        )
    )
    )sql"};
  if (!unlink_query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reusable extent unlink");
  }

  storage_error error = bind_physical_extent_state(database, unlink_query.get(), 1, physical_extent_state::garbage);
  if (!failed(error))
    error = bind_physical_extent_state(database, unlink_query.get(), 2, physical_extent_state::abandoned);
  if (failed(error)) return error;

  if (unlink_query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to unlink reusable extents");

  statement query{database, R"sql(
    UPDATE physical_extents
    SET
      state = ?1,
      operation_id = NULL,
      operation_ordinal = NULL
    WHERE (state = ?2
      OR state = ?3)
      AND NOT EXISTS (
        SELECT 1
        FROM cleanup_protected_extents
        WHERE cleanup_protected_extents.segment_id = physical_extents.segment_id
          AND cleanup_protected_extents.offset = physical_extents.offset
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reusable extent cleanup");

  error = bind_physical_extent_state(database, query.get(), 1, physical_extent_state::free);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::abandoned);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 3, physical_extent_state::garbage);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to normalize reusable extents");

  return {};
}

storage_error restore_interrupted_releases(sqlite3* database)
{
  statement query{database, R"sql(
    UPDATE physical_extents
    SET state = ?1
    WHERE state = ?2
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare interrupted extent release recovery");

  storage_error error = bind_physical_extent_state(database, query.get(), 1, physical_extent_state::free);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::releasing);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to recover interrupted extent releases");

  return {};
}

storage_error delete_orphaned_payloads(sqlite3* database)
{
  statement query{database, R"sql(
    DELETE FROM object_payloads
    WHERE NOT EXISTS (
      SELECT 1 FROM objects
      WHERE objects.payload_id = object_payloads.id
        AND objects.state != ?1
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare orphan payload cleanup");

  storage_error error = bind_object_state(database, query.get(), 1, object_state::deleted);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete orphan payloads");

  return {};
}

storage_error coalesce_free_extents(sqlite3* database)
{
  struct free_extent_record {
    std::int64_t id = 0;
    std::int64_t segment_id = 0;
    std::int64_t offset = 0;
    std::int64_t length = 0;
  };

  statement select_query{database, R"sql(
    SELECT
      id,
      segment_id,
      offset,
      length
    FROM physical_extents
    WHERE state = ?1
    ORDER BY segment_id,
      offset
    )sql"};
  if (!select_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent lookup");

  storage_error error = bind_physical_extent_state(database, select_query.get(), 1, physical_extent_state::free);
  if (failed(error)) return error;

  std::vector<free_extent_record> extents;
  int result = SQLITE_ROW;
  while ((result = select_query.step()) == SQLITE_ROW) {
    extents.push_back(free_extent_record{
        sqlite3_column_int64(select_query.get(), 0),
        sqlite3_column_int64(select_query.get(), 1),
        sqlite3_column_int64(select_query.get(), 2),
        sqlite3_column_int64(select_query.get(), 3),
    });
  }
  if (result != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read free extents");

  statement update_query{database, R"sql(
    UPDATE physical_extents
    SET
      length = ?2
    WHERE id = ?1
      AND state = ?3
    )sql"};
  if (!update_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent coalescing");

  statement delete_query{database, R"sql(
    DELETE FROM physical_extents
    WHERE id = ?1
      AND state = ?2
    )sql"};
  if (!delete_query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare adjacent free extent deletion");
  }

  std::size_t first = 0;
  while (first < extents.size()) {
    std::size_t next = first + 1;
    std::int64_t combined_length = extents[first].length;
    while (next < extents.size() && extents[next].segment_id == extents[first].segment_id &&
           extents[next].offset == extents[first].offset + combined_length) {
      combined_length += extents[next].length;

      delete_query.reset();
      error = bind_int64(database, delete_query.get(), 1, extents[next].id);
      if (!failed(error))
        error = bind_physical_extent_state(database, delete_query.get(), 2, physical_extent_state::free);
      if (failed(error)) return error;
      if (delete_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
        return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete adjacent free extent");
      ++next;
    }

    if (next != first + 1) {
      update_query.reset();
      error = bind_int64(database, update_query.get(), 1, extents[first].id);
      if (!failed(error)) error = bind_int64(database, update_query.get(), 2, combined_length);
      if (!failed(error))
        error = bind_physical_extent_state(database, update_query.get(), 3, physical_extent_state::free);
      if (failed(error)) return error;
      if (update_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1) {
        return make_sqlite_error(database, storage_error_code::index_failure,
                                 "failed to coalesce adjacent free extents");
      }
    }

    first = next;
  }

  return {};
}

storage_error claim_free_extents(sqlite3* database, storage_reclamation_plan& plan)
{
  storage_reclamation_plan pending;
  statement extents_query{database, R"sql(
    SELECT
      segment_id,
      offset,
      length
    FROM physical_extents
    WHERE state = ?1
    ORDER BY segment_id,
      offset
    )sql"};
  if (!extents_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent release");

  storage_error error = bind_physical_extent_state(database, extents_query.get(), 1, physical_extent_state::free);
  if (failed(error)) return error;
  int result = SQLITE_ROW;
  while ((result = extents_query.step()) == SQLITE_ROW) {
    physical_extent extent;
    extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 0));
    extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 1));
    extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(extents_query.get(), 2));
    extent.reserved_length = extent.length;
    pending.extents.push_back(extent);
  }
  if (result != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read free extents for release");

  statement segments_query{database, R"sql(
    SELECT DISTINCT candidate.segment_id
    FROM physical_extents AS candidate
    WHERE candidate.state = ?1
      AND NOT EXISTS (
        SELECT 1
        FROM physical_extents AS used
        WHERE used.segment_id = candidate.segment_id
          AND used.state != ?1
      )
    ORDER BY candidate.segment_id
    )sql"};
  if (!segments_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare fully free segment release");

  error = bind_physical_extent_state(database, segments_query.get(), 1, physical_extent_state::free);
  if (failed(error)) return error;
  result = SQLITE_ROW;
  while ((result = segments_query.step()) == SQLITE_ROW) {
    pending.fully_free_segment_ids.push_back(static_cast<std::uint64_t>(sqlite3_column_int64(segments_query.get(), 0)));
  }
  if (result != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to read fully free segments for release");

  statement claim_query{database, R"sql(
    UPDATE physical_extents
    SET state = ?1
    WHERE state = ?2
    )sql"};
  if (!claim_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent claim");

  error = bind_physical_extent_state(database, claim_query.get(), 1, physical_extent_state::releasing);
  if (!failed(error)) error = bind_physical_extent_state(database, claim_query.get(), 2, physical_extent_state::free);
  if (failed(error)) return error;
  if (claim_query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to claim free extents");

  plan = std::move(pending);
  return {};
}

} // namespace

namespace {

storage_error delete_corrupted_objects(sqlite3* database)
{
  statement extents{database, R"sql(
    UPDATE physical_extents
    SET state = ?1
    WHERE state = ?2
      AND id IN (
        SELECT payload_extent.physical_extent_id
        FROM payload_extents AS payload_extent
        JOIN objects AS object ON object.payload_id = payload_extent.payload_id
        WHERE object.state = ?3
          AND NOT EXISTS (
            SELECT 1
            FROM objects AS retained_object
            WHERE retained_object.payload_id = payload_extent.payload_id
              AND retained_object.state != ?3
              AND retained_object.state != ?4
          )
      )
    )sql"};
  if (!extents.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare corrupted extent deletion");
  storage_error error = bind_physical_extent_state(database, extents.get(), 1, physical_extent_state::garbage);
  if (!failed(error)) error = bind_physical_extent_state(database, extents.get(), 2, physical_extent_state::committed);
  if (!failed(error)) error = bind_object_state(database, extents.get(), 3, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, extents.get(), 4, object_state::deleted);
  if (failed(error)) return error;
  if (extents.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete corrupted extents");

  statement objects{database, R"sql(
    UPDATE objects
    SET state = ?1,
      payload_id = NULL
    WHERE state = ?2
    )sql"};
  if (!objects.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare corrupted object deletion");
  error = bind_object_state(database, objects.get(), 1, object_state::deleted);
  if (!failed(error)) error = bind_object_state(database, objects.get(), 2, object_state::corrupted);
  if (failed(error)) return error;
  if (objects.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete corrupted objects");

  return promote_recoverable_versions(database);
}

} // namespace

storage_error normalize_sqlite_index(sqlite3* database)
{
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  error = ensure_protected_extents_table(database);
  if (!failed(error)) error = abandon_incomplete_extents(database);
  if (!failed(error)) error = quarantine_invalid_current_objects(database);
  if (!failed(error)) error = promote_recoverable_versions(database);
  if (!failed(error)) error = restore_interrupted_releases(database);
  if (!failed(error)) error = normalize_reusable_extents(database);
  if (!failed(error)) error = delete_orphaned_payloads(database);
  if (!failed(error)) error = coalesce_free_extents(database);

  if (failed(error)) return error;
  return scope.commit();
}

storage_error prepare_reusable_extents(sqlite3* database, const std::vector<physical_extent>& protected_extents,
                                       const reclaim_storage_options& options, storage_reclamation_plan& plan,
                                       reclaim_storage_result& result)
{
  plan = {};
  result = {};
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  error = replace_protected_extents(database, protected_extents);
  if (!failed(error) && options.delete_corrupted_objects) error = delete_corrupted_objects(database);
  if (!failed(error)) error = restore_interrupted_releases(database);
  reclamation_estimate reclaimed;
  if (!failed(error)) error = normalize_reusable_extents(database, &reclaimed);
  if (!failed(error)) error = delete_orphaned_payloads(database);
  if (!failed(error)) error = coalesce_free_extents(database);

  reclamation_estimate remaining;
  if (!failed(error)) error = read_reclamation_estimate(database, remaining);

  storage_reclamation_plan pending;
  if (!failed(error)) error = claim_free_extents(database, pending);
  if (failed(error)) return error;

  error = scope.commit();
  if (!failed(error)) {
    plan = std::move(pending);
    result.reclaimed_bytes = reclaimed.reclaimable_bytes;
    result.reclaimed_extent_count = reclaimed.reclaimable_extent_count;
    result.remaining_reclaimable_bytes = remaining.reclaimable_bytes;
    result.remaining_reclaimable_extent_count = remaining.reclaimable_extent_count;
  }
  return error;
}

storage_error restore_absent_segment_extents(sqlite3* database, const storage_reclamation_plan& plan,
                                             std::uint64_t segment_capacity)
{
  if (segment_capacity == 0) return make_error(storage_error_code::index_failure, "absent segment capacity is zero");

  statement used_query{database, "SELECT COUNT(*) FROM physical_extents WHERE segment_id = ?1 AND state != ?2"};
  if (!used_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare absent segment validation");

  statement delete_query{database, "DELETE FROM physical_extents WHERE segment_id = ?1 AND state = ?2"};
  if (!delete_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare absent segment normalization");

  for (const std::uint64_t segment_id : plan.absent_segment_ids) {
    used_query.reset();
    storage_error error = bind_uint64(database, used_query.get(), 1, segment_id);
    if (!failed(error)) error = bind_physical_extent_state(database, used_query.get(), 2, physical_extent_state::free);
    if (failed(error)) return error;
    if (used_query.step() != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to validate absent segment");
    if (sqlite3_column_int64(used_query.get(), 0) != 0)
      return make_error(storage_error_code::index_failure, "absent segment contains a non-free extent");

    delete_query.reset();
    error = bind_uint64(database, delete_query.get(), 1, segment_id);
    if (!failed(error))
      error = bind_physical_extent_state(database, delete_query.get(), 2, physical_extent_state::free);
    if (failed(error)) return error;
    if (delete_query.step() != SQLITE_DONE)
      return make_sqlite_error(database, storage_error_code::index_failure,
                               "failed to normalize absent segment extents");

    const physical_extent extent{segment_id, 0, segment_capacity, segment_capacity};
    error = create_free_extent_record(database, extent);
    if (failed(error)) return error;
  }
  return {};
}

storage_error detect_empty_allocator(sqlite3* database, bool& result)
{
  statement query{database, "SELECT COUNT(*) FROM physical_extents"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare empty allocator detection");
  if (query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to inspect allocator extents");

  result = sqlite3_column_int64(query.get(), 0) == 0;
  return {};
}

storage_error finish_reusable_extents(sqlite3* database, const storage_reclamation_plan& plan,
                                      std::uint64_t segment_capacity, bool& allocator_reset)
{
  allocator_reset = false;
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  statement query{database, R"sql(
    UPDATE physical_extents
    SET state = ?4
    WHERE segment_id = ?1
      AND offset = ?2
      AND length = ?3
      AND state = ?5
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare released extent finalization");

  for (const physical_extent& extent : plan.extents) {
    query.reset();
    error = bind_uint64(database, query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.offset);
    if (!failed(error)) error = bind_uint64(database, query.get(), 3, extent.length);
    if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 4, physical_extent_state::free);
    if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 5, physical_extent_state::releasing);
    if (failed(error)) return error;
    if (query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to finalize released extent");
  }

  error = restore_absent_segment_extents(database, plan, segment_capacity);
  if (!failed(error)) error = detect_empty_allocator(database, allocator_reset);
  if (!failed(error) && !allocator_reset) error = coalesce_free_extents(database);

  if (failed(error)) return error;
  error = scope.commit();
  if (failed(error)) allocator_reset = false;
  return error;
}

} // namespace extora::core::sqlite_detail
