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

#include "sqlite_compaction_records.h"

#include <limits>

#include "sqlite_common.h"
#include "sqlite_extent_records.h"
#include "sqlite_states.h"

namespace extora::core::sqlite_detail {

namespace {

struct extent_record {
  std::int64_t id = 0;
  physical_extent extent;
};

storage_error payload_is_healthy(sqlite3* database, std::int64_t payload_id, bool& healthy)
{
  healthy = false;
  statement query{database, R"sql(
    SELECT 1
    FROM objects
    WHERE payload_id = ?1
      AND state != ?2
      AND state != ?3
    LIMIT 1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare compacted payload lookup");

  storage_error error = bind_int64(database, query.get(), 1, payload_id);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 3, object_state::deleted);
  if (failed(error)) return error;

  const int result = query.step();
  if (result == SQLITE_ROW) {
    healthy = true;
    return {};
  }
  if (result == SQLITE_DONE) return {};
  return make_sqlite_error(database, storage_error_code::index_failure, "failed to find compacted payload");
}

storage_error load_extent_records(sqlite3* database, std::int64_t payload_id, std::vector<extent_record>& records)
{
  records.clear();
  statement query{database, R"sql(
    SELECT
      physical.id,
      physical.segment_id,
      physical.offset,
      physical.length,
      physical.state
    FROM payload_extents AS payload_extent
    JOIN physical_extents AS physical ON physical.id = payload_extent.physical_extent_id
    WHERE payload_extent.payload_id = ?1
    ORDER BY payload_extent.ordinal
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare payload extent lookup");

  storage_error error = bind_int64(database, query.get(), 1, payload_id);
  if (failed(error)) return error;
  while (true) {
    const int result = query.step();
    if (result == SQLITE_DONE) return {};
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load payload extents");
    if (sqlite3_column_int(query.get(), 4) != static_cast<int>(physical_extent_state::committed))
      return make_error(storage_error_code::index_failure, "compacted payload extent is not committed");

    extent_record record;
    record.id = sqlite3_column_int64(query.get(), 0);
    record.extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    record.extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    record.extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 3));
    record.extent.reserved_length = record.extent.length;
    records.push_back(record);
  }
}

bool manifests_match(const std::vector<extent_record>& records, const std::vector<physical_extent>& expected_extents)
{
  if (records.size() != expected_extents.size()) return false;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const physical_extent& actual = records[index].extent;
    const physical_extent& expected = expected_extents[index];
    if (actual.segment_id != expected.segment_id || actual.offset != expected.offset ||
        actual.length != expected.length)
      return false;
  }
  return true;
}

bool same_extent(const physical_extent& left, const physical_extent& right)
{
  return left.segment_id == right.segment_id && left.offset == right.offset && left.length == right.length;
}

storage_error payload_length(const std::vector<physical_extent>& extents, std::uint64_t& length)
{
  length = 0;
  for (const physical_extent& extent : extents) {
    if (extent.length == 0 || extent.length > (std::numeric_limits<std::uint64_t>::max)() - length)
      return make_error(storage_error_code::index_failure, "compacted payload extent manifest is invalid");
    length += extent.length;
  }
  return {};
}

storage_error insert_payload_extent(sqlite3* database, std::int64_t payload_id, std::size_t ordinal,
                                    std::int64_t physical_extent_id)
{
  statement query{database, R"sql(
    INSERT INTO payload_extents (payload_id, ordinal, physical_extent_id)
    VALUES (?1, ?2, ?3)
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare compacted extent link");

  storage_error error = bind_int64(database, query.get(), 1, payload_id);
  if (!failed(error)) error = bind_uint64(database, query.get(), 2, ordinal);
  if (!failed(error)) error = bind_int64(database, query.get(), 3, physical_extent_id);
  if (failed(error)) return error;
  if (query.step() != SQLITE_DONE)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to link compacted extent");
  return {};
}

} // namespace

storage_error load_compaction_layout(sqlite3* database, storage_compaction_layout& layout)
{
  layout = {};
  statement query{database, R"sql(
    SELECT
      physical.segment_id,
      physical.offset,
      physical.length,
      physical.state,
      EXISTS (
        SELECT 1
        FROM payload_extents AS link
        JOIN objects AS object ON object.payload_id = link.payload_id
        WHERE link.physical_extent_id = physical.id
          AND object.state != ?1
          AND object.state != ?2
      )
    FROM physical_extents AS physical
    ORDER BY
      physical.segment_id,
      physical.offset
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare compaction layout lookup");

  storage_error error = bind_object_state(database, query.get(), 1, object_state::corrupted);
  if (!failed(error)) error = bind_object_state(database, query.get(), 2, object_state::deleted);
  if (failed(error)) return error;

  while (true) {
    const int result = query.step();
    if (result == SQLITE_DONE) return {};
    if (result != SQLITE_ROW)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to load compaction layout");

    compaction_extent item;
    item.extent.segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
    item.extent.offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    item.extent.length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    item.extent.reserved_length = item.extent.length;
    const physical_extent_state state = static_cast<physical_extent_state>(sqlite3_column_int(query.get(), 3));
    if (state == physical_extent_state::free)
      item.kind = compaction_extent_kind::reusable;
    else if (state == physical_extent_state::committed && sqlite3_column_int(query.get(), 4) != 0)
      item.kind = compaction_extent_kind::movable;
    else
      item.kind = compaction_extent_kind::fixed;
    layout.extents.push_back(item);
  }
}

storage_error replace_payload_extent_records(sqlite3* database, std::uint64_t payload_id,
                                             const std::vector<physical_extent>& expected_extents,
                                             const std::vector<physical_extent>& replacement_extents, bool& replaced,
                                             reclamation_estimate& reclaimable_delta)
{
  replaced = false;
  reclaimable_delta = {};
  if (payload_id == 0 || payload_id > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
    return make_error(storage_error_code::index_failure, "invalid compacted payload identifier");
  if (expected_extents.empty() || replacement_extents.empty())
    return make_error(storage_error_code::index_failure, "compacted payload extent manifest is empty");

  std::uint64_t expected_length = 0;
  storage_error error = payload_length(expected_extents, expected_length);
  std::uint64_t replacement_length = 0;
  if (!failed(error)) error = payload_length(replacement_extents, replacement_length);
  if (failed(error)) return error;
  if (expected_length != replacement_length)
    return make_error(storage_error_code::index_failure, "compacted payload length changed");

  transaction scope{database};
  error = scope.begin();
  if (failed(error)) return error;

  const std::int64_t signed_payload_id = static_cast<std::int64_t>(payload_id);
  bool healthy = false;
  error = payload_is_healthy(database, signed_payload_id, healthy);
  if (failed(error) || !healthy) return error;

  std::vector<extent_record> current_records;
  error = load_extent_records(database, signed_payload_id, current_records);
  if (failed(error) || !manifests_match(current_records, expected_extents)) return error;

  std::vector<std::int64_t> replacement_ids;
  replacement_ids.reserve(replacement_extents.size());
  std::vector<bool> retained_records(current_records.size(), false);
  for (const physical_extent& extent : replacement_extents) {
    bool retained = false;
    for (std::size_t index = 0; index < current_records.size(); ++index) {
      if (retained_records[index] || !same_extent(current_records[index].extent, extent)) continue;
      retained_records[index] = true;
      replacement_ids.push_back(current_records[index].id);
      retained = true;
      break;
    }
    if (retained) continue;

    std::int64_t extent_id = 0;
    error = commit_reserved_extent_record(database, extent, extent_id);
    if (failed(error)) return error;
    replacement_ids.push_back(extent_id);
  }

  statement unlink{database, "DELETE FROM payload_extents WHERE payload_id = ?1"};
  if (!unlink.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare payload extent switch");
  error = bind_int64(database, unlink.get(), 1, signed_payload_id);
  if (failed(error)) return error;
  if (unlink.step() != SQLITE_DONE || sqlite3_changes(database) != static_cast<int>(current_records.size()))
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to unlink compacted payload extents");

  for (std::size_t index = 0; index < replacement_ids.size(); ++index) {
    error = insert_payload_extent(database, signed_payload_id, index, replacement_ids[index]);
    if (failed(error)) return error;
  }

  statement release{database, R"sql(
    UPDATE physical_extents
    SET state = ?2
    WHERE id = ?1
      AND state = ?3
    )sql"};
  if (!release.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare old extent release");
  for (std::size_t index = 0; index < current_records.size(); ++index) {
    if (retained_records[index]) continue;
    const extent_record& record = current_records[index];
    release.reset();
    error = bind_int64(database, release.get(), 1, record.id);
    if (!failed(error)) error = bind_physical_extent_state(database, release.get(), 2, physical_extent_state::garbage);
    if (!failed(error))
      error = bind_physical_extent_state(database, release.get(), 3, physical_extent_state::committed);
    if (failed(error)) return error;
    if (release.step() != SQLITE_DONE || sqlite3_changes(database) != 1)
      return make_sqlite_error(database, storage_error_code::index_failure, "failed to release old payload extent");
    reclaimable_delta.reclaimable_bytes += record.extent.length;
    ++reclaimable_delta.reclaimable_extent_count;
  }

  error = scope.commit();
  if (failed(error)) return error;
  replaced = true;
  return {};
}

} // namespace extora::core::sqlite_detail
