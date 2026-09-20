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

#include "extora/core/sqlite_object_index.h"

#include <string>
#include <system_error>
#include <utility>

#include <sqlite3.h>

#include "sqlite_bucket_records.h"
#include "sqlite_common.h"
#include "sqlite_compaction_records.h"
#include "sqlite_extent_records.h"
#include "sqlite_multipart_records.h"
#include "sqlite_normalization.h"
#include "sqlite_object_records.h"
#include "sqlite_schema.h"

namespace extora::core {

using sqlite_detail::create_bucket_record;
using sqlite_detail::create_free_extent_record;
using sqlite_detail::execute;
using sqlite_detail::find_bucket_record;
using sqlite_detail::find_object_metadata_record;
using sqlite_detail::find_object_record;
using sqlite_detail::find_object_version_record;
using sqlite_detail::find_operation_extent_record;
using sqlite_detail::finish_reusable_extents;
using sqlite_detail::initialize_sqlite_schema;
using sqlite_detail::list_bucket_records;
using sqlite_detail::list_object_part_records;
using sqlite_detail::list_object_version_records;
using sqlite_detail::load_compaction_layout;
using sqlite_detail::make_sqlite_error;
using sqlite_detail::normalize_sqlite_index;
using sqlite_detail::prepare_reusable_extents;
using sqlite_detail::publish_object_record;
using sqlite_detail::read_bucket_usage;
using sqlite_detail::read_reclamation_estimate;
using sqlite_detail::replace_payload_extent_records;
using sqlite_detail::reserve_exact_reusable_extent_record;
using sqlite_detail::reserve_extent_record;
using sqlite_detail::reserve_reusable_extent_record;
using sqlite_detail::set_bucket_versioning_record;
using sqlite_detail::statement;

namespace {

constexpr int sqlite_busy_timeout_ms = 5000;
constexpr int sqlite_wal_autocheckpoint_pages = 1000;

const char* sqlite_synchronous_pragma(storage_durability durability)
{
  if (durability == storage_durability::strict) return R"sql(PRAGMA synchronous = FULL)sql";
  if (durability == storage_durability::balanced) return R"sql(PRAGMA synchronous = NORMAL)sql";

  return R"sql(PRAGMA synchronous = OFF)sql";
}

int sqlite_synchronous_value(storage_durability durability)
{
  if (durability == storage_durability::strict) return 2;
  if (durability == storage_durability::balanced) return 1;

  return 0;
}

storage_error read_pragma_text(sqlite3* database, const char* sql, std::string& value)
{
  statement query{database, sql};
  if (!query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare SQLite configuration query");
  }
  if (query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read SQLite configuration");

  value = sqlite_detail::column_text(query.get(), 0);
  return {};
}

storage_error read_pragma_int(sqlite3* database, const char* sql, int& value)
{
  statement query{database, sql};
  if (!query.prepared()) {
    return make_sqlite_error(database, storage_error_code::index_failure,
                             "failed to prepare SQLite configuration query");
  }
  if (query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read SQLite configuration");

  value = sqlite3_column_int(query.get(), 0);
  return {};
}

} // namespace

sqlite_object_index::sqlite_object_index(std::filesystem::path database_path, std::uint64_t segment_capacity,
                                         storage_durability durability)
  : m_database_path{std::move(database_path)}
  , m_segment_capacity{segment_capacity}
  , m_durability{durability}
{}

sqlite_object_index::~sqlite_object_index()
{
  for (sqlite3* database : m_read_databases)
    if (database != nullptr) sqlite3_close(database);
  if (m_database != nullptr) sqlite3_close(m_database);
}

sqlite_object_index::read_connection::read_connection(sqlite_object_index& index, storage_error& error)
{
  error = index.acquire_read_database(m_lock, m_database);
}

sqlite3* sqlite_object_index::read_connection::database() const
{
  return m_database;
}

storage_error sqlite_object_index::acquire_read_database(std::unique_lock<std::mutex>& lock, sqlite3*& database)
{
  const std::size_t first_slot =
      m_next_read_database.fetch_add(1, std::memory_order_relaxed) % read_connection_capacity;
  std::size_t slot = first_slot;
  for (std::size_t offset = 0; offset < read_connection_capacity; ++offset) {
    slot = (first_slot + offset) % read_connection_capacity;
    std::unique_lock<std::mutex> candidate{m_read_database_mutexes[slot], std::defer_lock};
    if (candidate.try_lock()) {
      lock = std::move(candidate);
      break;
    }
  }

  if (!lock.owns_lock()) {
    slot = first_slot;
    lock = std::unique_lock<std::mutex>{m_read_database_mutexes[slot]};
  }

  if (m_read_databases[slot] == nullptr) {
    storage_error error = open_read_database(m_read_databases[slot]);
    if (failed(error)) return error;
  }

  database = m_read_databases[slot];
  return {};
}

storage_error sqlite_object_index::open_read_database(sqlite3*& database)
{
  const std::string path{m_database_path.u8string()};
  const int result = sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
  if (result != SQLITE_OK) {
    const storage_error error =
        make_sqlite_error(database, storage_error_code::index_failure, "failed to open SQLite read connection");
    if (database != nullptr) sqlite3_close(database);
    database = nullptr;
    return error;
  }

  const auto close_on_error = [&](const storage_error& error) {
    sqlite3_close(database);
    database = nullptr;
    return error;
  };
  if (sqlite3_busy_timeout(database, sqlite_busy_timeout_ms) != SQLITE_OK) {
    return close_on_error(make_sqlite_error(database, storage_error_code::index_failure,
                                            "failed to configure SQLite read connection timeout"));
  }

  storage_error error = execute(database, R"sql(PRAGMA query_only = ON)sql");
  if (failed(error)) return close_on_error(error);
  error = execute(database, R"sql(PRAGMA cache_size = -256)sql");
  if (failed(error)) return close_on_error(error);
  return {};
}

storage_error sqlite_object_index::open()
{
  if (m_database != nullptr) return {};
  if (m_segment_capacity == 0) return make_error(storage_error_code::index_failure, "segment capacity is zero");

  std::error_code error_code;
  const std::filesystem::path parent = m_database_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error_code);
    if (error_code)
      return make_error(storage_error_code::index_failure, "failed to create index directory: " + error_code.message());
  }

#if defined(_WIN32)
  if (sqlite3_open16(m_database_path.c_str(), &m_database) != SQLITE_OK) {
#else
  if (sqlite3_open(m_database_path.c_str(), &m_database) != SQLITE_OK) {
#endif // defined(_WIN32)
    const storage_error error =
        make_sqlite_error(m_database, storage_error_code::index_failure, "failed to open SQLite index");
    if (m_database != nullptr) sqlite3_close(m_database);
    m_database = nullptr;
    return error;
  }

  const auto close_on_error = [&](const storage_error& error) {
    sqlite3_close(m_database);
    m_database = nullptr;
    return error;
  };

  if (sqlite3_busy_timeout(m_database, sqlite_busy_timeout_ms) != SQLITE_OK) {
    return close_on_error(
        make_sqlite_error(m_database, storage_error_code::index_failure, "failed to configure SQLite busy timeout"));
  }

  storage_error error = execute(m_database, R"sql(PRAGMA foreign_keys = ON)sql");
  if (failed(error)) return close_on_error(error);

  std::string journal_mode;
  error = read_pragma_text(m_database, R"sql(PRAGMA journal_mode = WAL)sql", journal_mode);
  if (failed(error)) return close_on_error(error);
  if (journal_mode != "wal")
    return close_on_error(make_error(storage_error_code::index_failure, "SQLite index did not enter WAL journal mode"));

  int autocheckpoint_pages = 0;
  error = read_pragma_int(m_database, R"sql(PRAGMA wal_autocheckpoint = 1000)sql", autocheckpoint_pages);
  if (failed(error)) return close_on_error(error);
  if (autocheckpoint_pages != sqlite_wal_autocheckpoint_pages) {
    return close_on_error(
        make_error(storage_error_code::index_failure, "SQLite WAL auto-checkpoint policy was not applied"));
  }

  error = execute(m_database, sqlite_synchronous_pragma(m_durability));
  if (failed(error)) return close_on_error(error);
  int synchronous = -1;
  error = read_pragma_int(m_database, R"sql(PRAGMA synchronous)sql", synchronous);
  if (failed(error)) return close_on_error(error);
  if (synchronous != sqlite_synchronous_value(m_durability)) {
    return close_on_error(
        make_error(storage_error_code::index_failure, "SQLite synchronization policy was not applied"));
  }

  error = initialize_sqlite_schema(m_database);
  if (failed(error)) return close_on_error(error);

  error = normalize_sqlite_index(m_database);
  if (failed(error)) return close_on_error(error);

  error = load_reclamation_estimate();
  if (failed(error)) return close_on_error(error);

  error = initialize_allocator();
  if (failed(error)) return close_on_error(error);
  return {};
}

storage_error sqlite_object_index::checkpoint()
{
  if (m_database == nullptr) return make_error(storage_error_code::index_failure, "SQLite index is not open");

  int log_frames = 0;
  int checkpointed_frames = 0;
  const int result =
      sqlite3_wal_checkpoint_v2(m_database, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log_frames, &checkpointed_frames);
  if (result != SQLITE_OK)
    return make_sqlite_error(m_database, storage_error_code::index_failure, "failed to checkpoint SQLite WAL");

  return {};
}

storage_error sqlite_object_index::create_bucket(const bucket_name& bucket)
{
  return create_bucket_record(m_database, bucket);
}

storage_error sqlite_object_index::delete_bucket(const bucket_name& bucket)
{
  return sqlite_detail::delete_bucket_record(m_database, bucket);
}

storage_error sqlite_object_index::find_bucket(const bucket_name& bucket, bucket_info& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return find_bucket_record(connection.database(), bucket, result);
}

storage_error sqlite_object_index::list_buckets(const list_buckets_options& options, bucket_list& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return list_bucket_records(connection.database(), options, result);
}

storage_error sqlite_object_index::get_bucket_usage(const bucket_name& bucket, bucket_usage& usage)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return read_bucket_usage(connection.database(), bucket, usage);
}

storage_error sqlite_object_index::set_bucket_versioning(const bucket_name& bucket, bucket_versioning_status status)
{
  return set_bucket_versioning_record(m_database, bucket, status);
}

storage_error sqlite_object_index::reserve_extent(std::string_view operation_id, std::uint64_t operation_ordinal,
                                                  std::uint64_t requested_length, physical_extent& extent,
                                                  extent_allocation_mode mode)
{
  extent = {};
  if (requested_length == 0) return {};
  if (operation_id.empty())
    return make_error(storage_error_code::index_failure, "extent operation identifier is empty");

  if (requested_length > m_segment_capacity)
    return make_error(storage_error_code::insufficient_space, "extent length is larger than segment capacity");
  if (mode != extent_allocation_mode::reuse && mode != extent_allocation_mode::contiguous)
    return make_error(storage_error_code::index_failure, "invalid extent allocation mode");

  // The FULL commit that publishes the object also persists its reservations.
  const bool defer_reservation_sync = m_durability == storage_durability::strict;
  if (defer_reservation_sync) {
    const storage_error synchronous_error = execute(m_database, R"sql(PRAGMA synchronous = NORMAL)sql");
    if (failed(synchronous_error)) return synchronous_error;
  }

  const auto restore_strict_sync = [&](storage_error error) {
    if (!defer_reservation_sync) return error;

    const storage_error restore_error = execute(m_database, R"sql(PRAGMA synchronous = FULL)sql");
    return failed(error) ? error : restore_error;
  };

  bool existing = false;
  storage_error error = find_operation_extent_record(m_database, operation_id, operation_ordinal, extent, existing);
  if (failed(error)) return restore_strict_sync(error);
  if (existing) {
    if (extent.length > requested_length) {
      return restore_strict_sync(
          make_error(storage_error_code::index_failure, "repeated extent reservation is smaller than the original"));
    }
    return restore_strict_sync({});
  }

  bool reused = false;
  error = reserve_reusable_extent_record(m_database, operation_id, operation_ordinal, requested_length,
                                         mode == extent_allocation_mode::reuse, extent, reused);
  if (failed(error) || reused) return restore_strict_sync(error);

  if (requested_length > m_segment_capacity - m_next_offset) {
    if (mode == extent_allocation_mode::reuse) {
      requested_length = m_segment_capacity - m_next_offset;
    } else {
      const physical_extent unused_tail{m_next_segment_id, m_next_offset, m_segment_capacity - m_next_offset,
                                        m_segment_capacity - m_next_offset};
      error = create_free_extent_record(m_database, unused_tail);
      if (failed(error)) return restore_strict_sync(error);
      ++m_next_segment_id;
      m_next_offset = 0;
    }
  }

  extent.segment_id = m_next_segment_id;
  extent.offset = m_next_offset;
  extent.length = requested_length;
  extent.reserved_length = requested_length;

  error = reserve_extent_record(m_database, operation_id, operation_ordinal, extent);
  if (failed(error)) return restore_strict_sync(error);

  m_next_offset += requested_length;
  if (m_next_offset == m_segment_capacity) {
    ++m_next_segment_id;
    m_next_offset = 0;
  }

  return restore_strict_sync({});
}

storage_error sqlite_object_index::reserve_extent_at(std::string_view operation_id, std::uint64_t operation_ordinal,
                                                     const physical_extent& requested_extent)
{
  if (operation_id.empty())
    return make_error(storage_error_code::index_failure, "extent operation identifier is empty");
  if (requested_extent.segment_id == 0 || requested_extent.length == 0 ||
      requested_extent.offset > m_segment_capacity ||
      requested_extent.length > m_segment_capacity - requested_extent.offset)
    return make_error(storage_error_code::index_failure, "exact extent reservation is outside its segment");

  physical_extent existing_extent;
  bool existing = false;
  storage_error error =
      find_operation_extent_record(m_database, operation_id, operation_ordinal, existing_extent, existing);
  if (failed(error)) return error;
  if (existing) {
    if (existing_extent.segment_id != requested_extent.segment_id ||
        existing_extent.offset != requested_extent.offset || existing_extent.length != requested_extent.length)
      return make_error(storage_error_code::index_failure, "repeated exact extent reservation changed");
    return {};
  }

  error = reserve_exact_reusable_extent_record(m_database, operation_id, operation_ordinal, requested_extent);
  return error;
}

storage_error sqlite_object_index::abandon_extents(const std::vector<physical_extent>& extents)
{
  reclamation_estimate reclaimable_delta;
  const storage_error error = sqlite_detail::abandon_extent_records(m_database, extents, reclaimable_delta);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::publish_object(const indexed_object& object, const object_conditions& conditions)
{
  if (m_durability == storage_durability::strict) {
    const storage_error error = execute(m_database, R"sql(PRAGMA synchronous = FULL)sql");
    if (failed(error)) return error;
  }

  reclamation_estimate reclaimable_delta;
  const storage_error error = publish_object_record(m_database, object, conditions, reclaimable_delta);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::find_dedup_object(const checksum_algorithm_name& checksum_algorithm,
                                                     std::string_view value, std::uint64_t content_length,
                                                     indexed_object& result)
{
  return sqlite_detail::find_dedup_object_record(m_database, checksum_algorithm, value, content_length, result);
}

storage_error sqlite_object_index::create_multipart_upload(const indexed_multipart_upload& upload)
{
  return sqlite_detail::create_multipart_upload_record(m_database, upload);
}

storage_error sqlite_object_index::find_multipart_upload(const multipart_upload_id& upload_id,
                                                         indexed_multipart_upload& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return sqlite_detail::find_multipart_upload_record(connection.database(), upload_id, result);
}

storage_error sqlite_object_index::store_multipart_part(const multipart_upload_id& upload_id, const bucket_name& bucket,
                                                        const object_key& key, const indexed_multipart_part& part)
{
  reclamation_estimate reclaimable_delta;
  const storage_error error =
      sqlite_detail::store_multipart_part_record(m_database, upload_id, bucket, key, part, reclaimable_delta);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::complete_multipart_upload(const multipart_upload_id& upload_id,
                                                             const indexed_object& object,
                                                             const object_conditions& conditions)
{
  reclamation_estimate reclaimable_delta;
  const storage_error error =
      sqlite_detail::publish_object_record(m_database, object, conditions, reclaimable_delta, &upload_id);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::abort_multipart_upload(const multipart_upload_id& upload_id,
                                                          const bucket_name& bucket, const object_key& key)
{
  reclamation_estimate reclaimable_delta;
  const storage_error error =
      sqlite_detail::abort_multipart_upload_record(m_database, upload_id, bucket, key, reclaimable_delta);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::list_multipart_uploads(const bucket_name& bucket,
                                                          std::vector<indexed_multipart_upload>& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return sqlite_detail::list_multipart_upload_records(connection.database(), bucket, result);
}

storage_error sqlite_object_index::find_object(const bucket_name& bucket, const object_key& key, indexed_object& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return find_object_record(connection.database(), bucket, key, result);
}

storage_error sqlite_object_index::find_object_version(const bucket_name& bucket, const object_key& key,
                                                       const object_version_id& version_id, indexed_object& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return find_object_version_record(connection.database(), bucket, key, version_id, result);
}

storage_error sqlite_object_index::find_object_metadata(const bucket_name& bucket, const object_key& key,
                                                        indexed_object& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return find_object_metadata_record(connection.database(), bucket, key, result);
}

storage_error sqlite_object_index::list_object_parts(const indexed_object& object, std::uint32_t part_number_marker,
                                                     std::size_t max_parts, indexed_object_part_page& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return list_object_part_records(connection.database(), object, part_number_marker, max_parts, result);
}

storage_error sqlite_object_index::delete_object(const bucket_name& bucket, const object_key& key,
                                                 const delete_object_options& options,
                                                 const object_version_id& marker_version_id,
                                                 delete_object_result& result)
{
  reclamation_estimate reclaimable_delta;
  const storage_error error = sqlite_detail::delete_object_record(m_database, bucket, key, options, marker_version_id,
                                                                  result, reclaimable_delta);
  if (!failed(error)) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::list_object_versions(const bucket_name& bucket,
                                                        const list_object_versions_options& options,
                                                        indexed_object_version_page& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return list_object_version_records(connection.database(), bucket, options, result);
}

storage_error sqlite_object_index::list_objects(const bucket_name& bucket, const list_objects_options& options,
                                                indexed_object_page& result)
{
  storage_error error;
  read_connection connection{*this, error};
  if (failed(error)) return error;
  return sqlite_detail::list_object_records(connection.database(), bucket, options, result);
}

storage_error sqlite_object_index::list_object_generations_for_recovery(std::vector<indexed_object>& result)
{
  return sqlite_detail::list_object_generation_records_for_recovery(m_database, result);
}

storage_error sqlite_object_index::mark_payload_corrupted(std::uint64_t payload_id)
{
  return sqlite_detail::mark_payload_corrupted_record(m_database, payload_id);
}

storage_error sqlite_object_index::replace_payload_extents(std::uint64_t payload_id,
                                                           const std::vector<physical_extent>& expected_extents,
                                                           const std::vector<physical_extent>& replacement_extents,
                                                           bool& replaced)
{
  if (m_durability == storage_durability::strict) {
    const storage_error error = execute(m_database, R"sql(PRAGMA synchronous = FULL)sql");
    if (failed(error)) return error;
  }

  reclamation_estimate reclaimable_delta;
  const storage_error error = replace_payload_extent_records(m_database, payload_id, expected_extents,
                                                             replacement_extents, replaced, reclaimable_delta);
  if (!failed(error) && replaced) apply_reclamation_delta(reclaimable_delta);
  return error;
}

storage_error sqlite_object_index::prepare_compaction(storage_compaction_layout& layout)
{
  if (m_next_offset != 0) {
    const physical_extent unused_tail{m_next_segment_id, m_next_offset, m_segment_capacity - m_next_offset,
                                      m_segment_capacity - m_next_offset};
    const storage_error error = create_free_extent_record(m_database, unused_tail);
    if (failed(error)) return error;
    ++m_next_segment_id;
    m_next_offset = 0;
  }
  return load_compaction_layout(m_database, layout);
}

storage_error sqlite_object_index::get_reclamation_estimate(reclamation_estimate& estimate)
{
  estimate = m_reclamation_estimate;
  return {};
}

storage_error sqlite_object_index::prepare_reclamation(const std::vector<physical_extent>& protected_extents,
                                                       const reclaim_storage_options& options,
                                                       storage_reclamation_plan& plan, reclaim_storage_result& result)
{
  const storage_error error = prepare_reusable_extents(m_database, protected_extents, options, plan, result);
  if (failed(error)) return error;
  return load_reclamation_estimate();
}

storage_error sqlite_object_index::finish_reclamation(const storage_reclamation_plan& plan)
{
  bool allocator_reset = false;
  const storage_error error = finish_reusable_extents(m_database, plan, m_segment_capacity, allocator_reset);
  if (failed(error)) return error;
  if (allocator_reset) {
    m_next_segment_id = 1;
    m_next_offset = 0;
    return {};
  }
  if (!plan.absent_segment_ids.empty()) return initialize_allocator();
  return {};
}

storage_error sqlite_object_index::load_reclamation_estimate()
{
  reclamation_estimate estimate;
  const storage_error error = read_reclamation_estimate(m_database, estimate);
  if (failed(error)) return error;

  m_reclamation_estimate = estimate;
  return {};
}

void sqlite_object_index::apply_reclamation_delta(const reclamation_estimate& delta)
{
  m_reclamation_estimate.reclaimable_bytes += delta.reclaimable_bytes;
  m_reclamation_estimate.reclaimable_extent_count += delta.reclaimable_extent_count;
}

storage_error sqlite_object_index::initialize_allocator()
{
  m_next_segment_id = 1;
  m_next_offset = 0;

  statement query{m_database, R"sql(
    SELECT
      segment_id,
      offset,
      length
    FROM physical_extents
    ORDER BY segment_id DESC,
      offset DESC
    LIMIT 1
    )sql"};
  if (!query.prepared())
    return make_sqlite_error(m_database, storage_error_code::index_failure, "failed to prepare allocator query");

  const int result = query.step();
  if (result == SQLITE_ROW) {
    m_next_segment_id = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0));
    m_next_offset = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 1));
    const std::uint64_t length = static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 2));
    m_next_offset += length;

    if (m_next_offset > m_segment_capacity)
      return make_error(storage_error_code::index_failure, "stored extent exceeds segment capacity");

    if (m_next_offset == m_segment_capacity) {
      ++m_next_segment_id;
      m_next_offset = 0;
    }
  } else if (result != SQLITE_DONE) {
    return make_sqlite_error(m_database, storage_error_code::index_failure, "failed to initialize allocator");
  }

  return {};
}

} // namespace extora::core
