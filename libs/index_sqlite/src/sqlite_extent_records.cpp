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

#include "sqlite_extent_records.h"

#include "sqlite_common.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error find_reusable_extent_candidate(sqlite3* database, std::uint64_t requested_length,
                                             bool require_sufficient_length, std::int64_t& extent_id,
                                             physical_extent& extent, bool& found)
{
  found = false;
  const char* sql = require_sufficient_length ? R"sql(
    SELECT
      id,
      segment_id,
      offset,
      length
    FROM physical_extents
    WHERE state = ?2
      AND length >= ?1
    ORDER BY
      length,
      segment_id,
      offset
    LIMIT 1
    )sql"
                                              : R"sql(
    SELECT
      id,
      segment_id,
      offset,
      length
    FROM physical_extents
    WHERE state = ?2
      AND length = (
        SELECT MAX(length)
        FROM physical_extents
        WHERE state = ?2
          AND length < ?1
      )
    ORDER BY
      segment_id,
      offset
    LIMIT 1
    )sql";
  statement query{database, sql};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent lookup");

  storage_error error = bind_uint64(database, query.get(), 1, requested_length);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::free);
  if (failed(error)) return error;

  const int result = query.step();
  if (result == SQLITE_DONE) return {};
  if (result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find free extent");

  extent_id = sqlite3_column_int64(query.get(), 0);
  extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
  extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
  extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 3));
  extent.reserved_length = extent.length;
  found = true;
  return {};
}

} // namespace

storage_error find_operation_extent_record(sqlite3* database, std::string_view operation_id,
                                           std::uint64_t operation_ordinal, physical_extent& extent, bool& found)
{
  found = false;
  statement query{database, R"sql(
    SELECT
      segment_id,
      offset,
      length,
      state
    FROM physical_extents
    WHERE operation_id = ?1
      AND operation_ordinal = ?2
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare operation extent lookup");

  storage_error error = bind_text(database, query.get(), 1, operation_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, operation_ordinal);
  if (failed(error)) return error;

  const int result = query.step();
  if (result == SQLITE_DONE) return {};
  if (result != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to find operation extent");
  if (sqlite3_column_int(query.get(), 3) != static_cast<int>(physical_extent_state::reserved))
    return make_error(storage_error_code::index_failure, "operation extent is already finalized");

  extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
  extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
  extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
  extent.reserved_length = extent.length;
  found = true;
  return {};
}

storage_error reserve_reusable_extent_record(sqlite3* database, std::string_view operation_id,
                                             std::uint64_t operation_ordinal, std::uint64_t requested_length,
                                             bool allow_partial, physical_extent& extent, bool& found)
{
  found = false;

  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  std::int64_t extent_id = 0;
  bool candidate_found = false;
  error = find_reusable_extent_candidate(database, requested_length, true, extent_id, extent, candidate_found);
  if (!failed(error) && !candidate_found && allow_partial)
    error = find_reusable_extent_candidate(database, requested_length, false, extent_id, extent, candidate_found);
  if (failed(error) || !candidate_found) return error;

  const std::uint64_t original_length = extent.length;
  extent.length = original_length < requested_length ? original_length : requested_length;
  extent.reserved_length = extent.length;
  statement update_query{database, R"sql(
    UPDATE physical_extents
    SET
      length = ?2,
      state = ?3,
      operation_id = ?5,
      operation_ordinal = ?6
    WHERE id = ?1
      AND state = ?4
    )sql"};
  if (!update_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent reservation");

  error = bind_int64(database, update_query.get(), 1, extent_id);
  if (!failed(error)) error = bind_uint64(database, update_query.get(), 2, extent.length);
  if (!failed(error))
    error = bind_physical_extent_state(database, update_query.get(), 3, physical_extent_state::reserved);
  if (!failed(error)) error = bind_physical_extent_state(database, update_query.get(), 4, physical_extent_state::free);
  if (!failed(error)) error = bind_text(database, update_query.get(), 5, operation_id);
  if (!failed(error)) error = bind_uint64(database, update_query.get(), 6, operation_ordinal);
  if (failed(error)) return error;

  if (update_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to reserve free extent");

  if (original_length > extent.length) {
    const std::uint64_t remainder_offset = extent.offset + extent.length;
    const std::uint64_t remainder_length = original_length - extent.length;
    statement remainder_query{database, R"sql(
      INSERT INTO physical_extents (
        segment_id,
        offset,
        length,
        state
      )
      VALUES (
        ?1,
        ?2,
        ?3,
        ?4
      )
      )sql"};
    if (!remainder_query.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent remainder");

    error = bind_uint64(database, remainder_query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, remainder_query.get(), 2, remainder_offset);
    if (!failed(error)) error = bind_uint64(database, remainder_query.get(), 3, remainder_length);
    if (!failed(error))
      error = bind_physical_extent_state(database, remainder_query.get(), 4, physical_extent_state::free);
    if (failed(error)) return error;

    if (remainder_query.step() != SQLITE_DONE)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to create free extent remainder");
  }

  error = scope.commit();
  if (failed(error)) return error;

  found = true;
  return {};
}

storage_error reserve_exact_reusable_extent_record(sqlite3* database, std::string_view operation_id,
                                                   std::uint64_t operation_ordinal,
                                                   const physical_extent& requested_extent)
{
  if (requested_extent.length == 0)
    return make_error(storage_error_code::index_failure, "exact extent reservation is empty");

  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  std::int64_t free_extent_id = 0;
  physical_extent free_extent;
  {
    statement query{database, R"sql(
      SELECT
        id,
        offset,
        length
      FROM physical_extents
      WHERE segment_id = ?1
        AND state = ?2
        AND offset <= ?3
        AND offset + length >= ?4
      ORDER BY offset DESC
      LIMIT 1
      )sql"};
    if (!query.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure,
                               "failed to prepare exact free extent lookup");

    error = bind_uint64(database, query.get(), 1, requested_extent.segment_id);
    if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::free);
    if (!failed(error)) error = bind_uint64(database, query.get(), 3, requested_extent.offset);
    if (!failed(error))
      error = bind_uint64(database, query.get(), 4, requested_extent.offset + requested_extent.length);
    if (failed(error)) return error;
    if (query.step() != SQLITE_ROW)
      return make_error(storage_error_code::index_failure, "requested compaction range is not reusable");

    free_extent_id = sqlite3_column_int64(query.get(), 0);
    free_extent.segment_id = requested_extent.segment_id;
    free_extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    free_extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    free_extent.reserved_length = free_extent.length;
  }

  statement erase{database, "DELETE FROM physical_extents WHERE id = ?1 AND state = ?2"};
  if (!erase.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare exact free extent removal");
  error = bind_int64(database, erase.get(), 1, free_extent_id);
  if (!failed(error)) error = bind_physical_extent_state(database, erase.get(), 2, physical_extent_state::free);
  if (failed(error)) return error;
  if (erase.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to remove exact free extent");

  const physical_extent prefix{free_extent.segment_id, free_extent.offset, requested_extent.offset - free_extent.offset,
                               requested_extent.offset - free_extent.offset};
  error = create_free_extent_record(database, prefix);
  if (failed(error)) return error;

  error = reserve_extent_record(database, operation_id, operation_ordinal, requested_extent);
  if (failed(error)) return error;

  const std::uint64_t requested_end = requested_extent.offset + requested_extent.length;
  const std::uint64_t free_end = free_extent.offset + free_extent.length;
  const physical_extent suffix{free_extent.segment_id, requested_end, free_end - requested_end,
                               free_end - requested_end};
  error = create_free_extent_record(database, suffix);
  if (failed(error)) return error;

  return scope.commit();
}

storage_error create_free_extent_record(sqlite3* database, const physical_extent& extent)
{
  if (extent.length == 0) return {};

  statement query{database, R"sql(
    INSERT INTO physical_extents (
      segment_id,
      offset,
      length,
      state
    )
    VALUES (
      ?1,
      ?2,
      ?3,
      ?4
    )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare free extent creation");

  storage_error error = bind_uint64(database, query.get(), 1, extent.segment_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.offset);
  if (!failed(error)) error = bind_uint64(database, query.get(), 3, extent.length);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 4, physical_extent_state::free);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to create free extent");

  return {};
}

storage_error reserve_extent_record(sqlite3* database, std::string_view operation_id, std::uint64_t operation_ordinal,
                                    const physical_extent& extent)
{
  statement query{database, R"sql(
    INSERT INTO physical_extents (
      segment_id,
      offset,
      length,
      state,
      operation_id,
      operation_ordinal
    )
    VALUES (
      ?1,
      ?2,
      ?3,
      ?4,
      ?5,
      ?6
    )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare extent reservation");

  storage_error error = bind_uint64(database, query.get(), 1, extent.segment_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.offset);
  if (!failed(error)) error = bind_uint64(database, query.get(), 3, extent.length);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 4, physical_extent_state::reserved);
  if (!failed(error)) error = bind_text(database, query.get(), 5, operation_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 6, operation_ordinal);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to reserve extent");

  return {};
}

namespace {

storage_error finalize_reserved_extent_record(sqlite3* database, const physical_extent& extent,
                                              physical_extent_state target_state, std::int64_t& physical_extent_id)
{
  std::uint64_t reserved_length = 0;
  storage_error error;
  {
    statement select_query{database, R"sql(
      SELECT
        id,
        length
      FROM physical_extents
      WHERE segment_id = ?1
        AND offset = ?2
        AND state = ?3
      )sql"};
    if (!select_query.prepared())
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reserved extent lookup");

    error = bind_uint64(database, select_query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, select_query.get(), 2, extent.offset);
    if (!failed(error))
      error = bind_physical_extent_state(database, select_query.get(), 3, physical_extent_state::reserved);
    if (failed(error)) return error;

    if (select_query.step() != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to find reserved extent");

    physical_extent_id = sqlite3_column_int64(select_query.get(), 0);
    reserved_length = static_cast<std::uint64_t>(sqlite3_column_int64(select_query.get(), 1));
  }

  if (extent.length == 0 || extent.length > reserved_length)
    return make_error(storage_error_code::index_failure, "extent length is outside reservation");

  statement query{database, R"sql(
    UPDATE physical_extents
    SET
      length = ?2,
      state = ?3
    WHERE id = ?1
      AND state = ?4
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare extent finalization");

  error = bind_int64(database, query.get(), 1, physical_extent_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, extent.length);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 3, target_state);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 4, physical_extent_state::reserved);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to finalize reserved extent");

  if (extent.length == reserved_length) return {};

  const std::uint64_t free_offset = extent.offset + extent.length;
  std::uint64_t free_length = reserved_length - extent.length;
  const std::uint64_t following_offset = extent.offset + reserved_length;
  std::int64_t following_id = 0;
  {
    statement following_query{database, R"sql(
      SELECT
        id,
        length
      FROM physical_extents
      WHERE segment_id = ?1
        AND offset = ?2
        AND state = ?3
      )sql"};
    if (!following_query.prepared()) {
      return make_sqlite_error(database, storage_error_code::index_failure,
                               "failed to prepare following free extent lookup");
    }

    error = bind_uint64(database, following_query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, following_query.get(), 2, following_offset);
    if (!failed(error))
      error = bind_physical_extent_state(database, following_query.get(), 3, physical_extent_state::free);
    if (failed(error)) return error;

    const int following_result = following_query.step();
    if (following_result == SQLITE_ROW) {
      following_id = sqlite3_column_int64(following_query.get(), 0);
      free_length += static_cast<std::uint64_t>(sqlite3_column_int64(following_query.get(), 1));
    } else if (following_result != SQLITE_DONE) {
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to find following free extent");
    }
  }

  if (following_id != 0) {
    statement delete_query{database, "DELETE FROM physical_extents WHERE id = ?1 AND state = ?2"};
    if (!delete_query.prepared()) {
      return make_sqlite_error(database, storage_error_code::index_failure,
                               "failed to prepare following free extent deletion");
    }

    error = bind_int64(database, delete_query.get(), 1, following_id);
    if (!failed(error))
      error = bind_physical_extent_state(database, delete_query.get(), 2, physical_extent_state::free);
    if (failed(error)) return error;
    if (delete_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to delete following free extent");
  }

  statement free_query{database, R"sql(
    INSERT INTO physical_extents (
      segment_id,
      offset,
      length,
      state
    )
    VALUES (
      ?1,
      ?2,
      ?3,
      ?4
    )
    )sql"};
  if (!free_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare unused extent tail");

  error = bind_uint64(database, free_query.get(), 1, extent.segment_id);
  if (!failed(error)) error = bind_uint64(database, free_query.get(), 2, free_offset);
  if (!failed(error)) error = bind_uint64(database, free_query.get(), 3, free_length);
  if (!failed(error)) error = bind_physical_extent_state(database, free_query.get(), 4, physical_extent_state::free);
  if (failed(error)) return error;

  if (free_query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to create unused extent tail");

  return {};
}

} // namespace

storage_error commit_reserved_extent_record(sqlite3* database, const physical_extent& extent,
                                            std::int64_t& physical_extent_id)
{
  return finalize_reserved_extent_record(database, extent, physical_extent_state::committed, physical_extent_id);
}

storage_error trim_reserved_extent_record(sqlite3* database, const physical_extent& extent,
                                          std::int64_t& physical_extent_id)
{
  return finalize_reserved_extent_record(database, extent, physical_extent_state::reserved, physical_extent_id);
}

storage_error abandon_extent_records(sqlite3* database, const std::vector<physical_extent>& extents,
                                     reclamation_estimate& reclaimable_delta)
{
  reclaimable_delta = {};
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  statement update_query{database, R"sql(
    UPDATE physical_extents
    SET
      state = ?3
    WHERE segment_id = ?1
      AND offset = ?2
      AND state = ?4
    )sql"};
  if (!update_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare extent abandonment");

  for (const physical_extent& extent : extents) {
    if (extent.reserved_length == 0)
      return make_error(storage_error_code::index_failure, "abandoned extent reservation length is missing");
    reclaimable_delta.reclaimable_bytes += extent.reserved_length;
    ++reclaimable_delta.reclaimable_extent_count;

    update_query.reset();
    error = bind_uint64(database, update_query.get(), 1, extent.segment_id);
    if (!failed(error)) error = bind_uint64(database, update_query.get(), 2, extent.offset);
    if (!failed(error))
      error = bind_physical_extent_state(database, update_query.get(), 3, physical_extent_state::abandoned);
    if (!failed(error))
      error = bind_physical_extent_state(database, update_query.get(), 4, physical_extent_state::reserved);
    if (failed(error)) return error;

    if (update_query.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to abandon reserved extent");
  }

  return scope.commit();
}

storage_error mark_payload_extents_garbage(sqlite3* database, std::int64_t object_id,
                                           reclamation_estimate& reclaimable_delta)
{
  reclaimable_delta = {};
  statement candidates_query{database, R"sql(
    SELECT
      COALESCE(SUM(length), 0),
      COUNT(*)
    FROM physical_extents
    WHERE id IN (
      SELECT physical_extent_id
      FROM payload_extents AS payload_extent
      JOIN objects AS object ON object.payload_id = payload_extent.payload_id
      WHERE object.id = ?1
    )
      AND state = ?2
      AND NOT EXISTS (
        SELECT 1
        FROM objects AS object
        JOIN objects AS other_object ON other_object.payload_id = object.payload_id
        WHERE object.id = ?1
          AND other_object.id != ?1
          AND other_object.state != ?3
      )
    )sql"};
  if (!candidates_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare garbage extent count");

  storage_error error = bind_int64(database, candidates_query.get(), 1, object_id);
  if (!failed(error))
    error = bind_physical_extent_state(database, candidates_query.get(), 2, physical_extent_state::committed);
  if (!failed(error)) error = bind_object_state(database, candidates_query.get(), 3, object_state::deleted);
  if (failed(error)) return error;
  if (candidates_query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to count garbage extents");

  reclaimable_delta.reclaimable_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(candidates_query.get(), 0));
  reclaimable_delta.reclaimable_extent_count =
      static_cast<std::uint64_t>(sqlite3_column_int64(candidates_query.get(), 1));

  statement query{database, R"sql(
    UPDATE physical_extents
    SET
      state = ?2
    WHERE id IN (
      SELECT physical_extent_id
      FROM payload_extents AS payload_extent
      JOIN objects AS object ON object.payload_id = payload_extent.payload_id
      WHERE object.id = ?1
    )
      AND state = ?3
      AND NOT EXISTS (
        SELECT 1
        FROM objects AS object
        JOIN objects AS other_object ON other_object.payload_id = object.payload_id
        WHERE object.id = ?1
          AND other_object.id != ?1
          AND other_object.state != ?4
      )
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare garbage extents");

  error = bind_int64(database, query.get(), 1, object_id);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 2, physical_extent_state::garbage);
  if (!failed(error)) error = bind_physical_extent_state(database, query.get(), 3, physical_extent_state::committed);
  if (!failed(error)) error = bind_object_state(database, query.get(), 4, object_state::deleted);
  if (failed(error)) return error;

  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to mark payload extents garbage");

  return {};
}

storage_error read_reclamation_estimate(sqlite3* database, reclamation_estimate& estimate)
{
  statement query{database, R"sql(
    SELECT
      COALESCE(SUM(length), 0),
      COUNT(*)
    FROM physical_extents
    WHERE state IN (3, 4)
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare reclamation estimate");

  if (query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read reclamation estimate");

  reclamation_estimate result;
  result.reclaimable_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
  result.reclaimable_extent_count = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
  estimate = result;
  return {};
}

} // namespace extora::core::sqlite_detail
