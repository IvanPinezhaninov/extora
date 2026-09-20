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

#include "sqlite_schema.h"

#include "sqlite_common.h"

namespace extora::core::sqlite_detail {

namespace {

constexpr int schema_version = 1;

storage_error bootstrap_schema(sqlite3* database)
{
  transaction scope{database};
  storage_error error = scope.begin();
  if (failed(error)) return error;

  error = execute(database, R"sql(
      CREATE TABLE buckets (
        id INTEGER PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        created_at INTEGER NOT NULL,
        versioning_enabled INTEGER NOT NULL DEFAULT 0
    )
    )sql");

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE object_payloads (
        id INTEGER PRIMARY KEY,
        content_length INTEGER NOT NULL,
        internal_checksum_algorithm TEXT NOT NULL,
        internal_checksum_value TEXT NOT NULL,
        dedup_algorithm TEXT,
        dedup_value TEXT
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE objects (
        id INTEGER PRIMARY KEY,
        bucket_id INTEGER NOT NULL,
        payload_id INTEGER,
        key TEXT NOT NULL,
        content_type TEXT,
        cache_control TEXT,
        content_disposition TEXT,
        content_encoding TEXT,
        content_language TEXT,
        expires_at INTEGER,
        checksum_algorithm TEXT,
        checksum_value TEXT,
        checksum_type INTEGER NOT NULL DEFAULT 1 CHECK (checksum_type IN (1, 2)),
        etag TEXT NOT NULL,
        version_id TEXT,
        is_delete_marker INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL,
        modified_at INTEGER NOT NULL,
        generation INTEGER NOT NULL,
        state INTEGER NOT NULL,
        UNIQUE (bucket_id, key, generation),
        UNIQUE (bucket_id, key, version_id),
        FOREIGN KEY (bucket_id) REFERENCES buckets (id),
        FOREIGN KEY (payload_id) REFERENCES object_payloads (id) ON DELETE SET NULL
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE physical_extents (
        id INTEGER PRIMARY KEY,
        segment_id INTEGER NOT NULL,
        offset INTEGER NOT NULL,
        length INTEGER NOT NULL,
        state INTEGER NOT NULL,
        operation_id TEXT,
        operation_ordinal INTEGER,
        UNIQUE (segment_id, offset),
        UNIQUE (operation_id, operation_ordinal),
        CHECK (
          (operation_id IS NULL AND operation_ordinal IS NULL)
          OR
          (operation_id IS NOT NULL AND operation_ordinal IS NOT NULL)
        )
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE payload_extents (
        payload_id INTEGER NOT NULL,
        ordinal INTEGER NOT NULL,
        physical_extent_id INTEGER NOT NULL,
        PRIMARY KEY (payload_id, ordinal),
        FOREIGN KEY (payload_id) REFERENCES object_payloads (id) ON DELETE CASCADE,
        FOREIGN KEY (physical_extent_id) REFERENCES physical_extents (id)
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE object_metadata (
        object_id INTEGER NOT NULL,
        ordinal INTEGER NOT NULL,
        name TEXT NOT NULL,
        value TEXT NOT NULL,
        PRIMARY KEY (object_id, ordinal),
        FOREIGN KEY (object_id) REFERENCES objects (id) ON DELETE CASCADE
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE object_parts (
        object_id INTEGER NOT NULL,
        part_number INTEGER NOT NULL,
        offset INTEGER NOT NULL,
        content_length INTEGER NOT NULL,
        checksum_algorithm TEXT NOT NULL,
        checksum_value TEXT NOT NULL,
        checksum_type INTEGER NOT NULL CHECK (checksum_type = 1),
        PRIMARY KEY (object_id, part_number),
        CHECK (part_number BETWEEN 1 AND 10000),
        CHECK (offset >= 0),
        CHECK (content_length >= 0),
        FOREIGN KEY (object_id) REFERENCES objects (id) ON DELETE CASCADE
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE multipart_uploads (
        upload_id TEXT PRIMARY KEY,
        bucket_id INTEGER NOT NULL,
        key TEXT NOT NULL,
        content_type TEXT,
        cache_control TEXT,
        content_disposition TEXT,
        content_encoding TEXT,
        content_language TEXT,
        expires_at INTEGER,
        checksum_algorithm TEXT NOT NULL,
        checksum_type INTEGER NOT NULL CHECK (checksum_type IN (1, 2)),
        deduplication INTEGER NOT NULL,
        initiated_at INTEGER NOT NULL,
        FOREIGN KEY (bucket_id) REFERENCES buckets (id)
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE multipart_upload_metadata (
        upload_id TEXT NOT NULL,
        ordinal INTEGER NOT NULL,
        name TEXT NOT NULL,
        value TEXT NOT NULL,
        PRIMARY KEY (upload_id, ordinal),
        FOREIGN KEY (upload_id) REFERENCES multipart_uploads (upload_id) ON DELETE CASCADE
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE multipart_parts (
        upload_id TEXT NOT NULL,
        part_number INTEGER NOT NULL,
        etag TEXT NOT NULL,
        content_length INTEGER NOT NULL,
        internal_checksum_algorithm TEXT NOT NULL,
        internal_checksum_value TEXT NOT NULL,
        checksum_algorithm TEXT,
        checksum_value TEXT,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (upload_id, part_number),
        FOREIGN KEY (upload_id) REFERENCES multipart_uploads (upload_id) ON DELETE CASCADE
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE TABLE multipart_part_extents (
        upload_id TEXT NOT NULL,
        part_number INTEGER NOT NULL,
        ordinal INTEGER NOT NULL,
        physical_extent_id INTEGER NOT NULL,
        PRIMARY KEY (upload_id, part_number, ordinal),
        FOREIGN KEY (upload_id, part_number)
          REFERENCES multipart_parts (upload_id, part_number) ON DELETE CASCADE,
        FOREIGN KEY (physical_extent_id) REFERENCES physical_extents (id)
      )
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX objects_bucket_key_state_idx
      ON objects (bucket_id, key, state)
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX objects_bucket_state_key_idx
      ON objects (bucket_id, state, key)
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX object_payloads_dedup_idx
      ON object_payloads (dedup_algorithm, dedup_value, content_length)
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX multipart_uploads_bucket_key_idx
      ON multipart_uploads (bucket_id, key, upload_id)
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX objects_payload_state_idx
      ON objects (payload_id, state)
      )sql");
  }

  if (!failed(error)) {
    error = execute(database, R"sql(
      CREATE INDEX physical_extents_free_length_location_idx
      ON physical_extents (length, segment_id, offset)
      WHERE state = 6
      )sql");
  }

  if (!failed(error)) error = execute(database, "PRAGMA user_version = 1");

  if (failed(error)) return error;
  return scope.commit();
}

} // namespace

storage_error initialize_sqlite_schema(sqlite3* database)
{
  statement version_query{database, R"sql(
    PRAGMA user_version
    )sql"};
  if (!version_query.prepared())
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to prepare schema version query");

  if (version_query.step() != SQLITE_ROW)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to read schema version");

  const int current_version = sqlite3_column_int(version_query.get(), 0);
  if (current_version > schema_version)
    return make_error(storage_error_code::index_failure, "SQLite index schema version is newer than this library");

  if (current_version == 0) return bootstrap_schema(database);

  if (current_version == schema_version) return {};

  return make_error(storage_error_code::index_failure, "SQLite index schema version is unsupported");
}

} // namespace extora::core::sqlite_detail
