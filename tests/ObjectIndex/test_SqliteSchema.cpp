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

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

TEST(SqliteIndexSchemaTest, SeparatesNamedObjectsPayloadsAndPhysicalExtents)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  EXPECT_TRUE(tableHasColumn(databasePath, "payload_extents", "physical_extent_id"));
  EXPECT_FALSE(tableHasColumn(databasePath, "payload_extents", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "payload_extents", "payload_id"));

  EXPECT_TRUE(tableHasColumn(databasePath, "physical_extents", "segment_id"));
  EXPECT_TRUE(tableHasColumn(databasePath, "physical_extents", "offset"));
  EXPECT_TRUE(tableHasColumn(databasePath, "physical_extents", "length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "physical_extents", "operation_id"));
  EXPECT_TRUE(tableHasColumn(databasePath, "physical_extents", "operation_ordinal"));
  EXPECT_FALSE(tableHasColumn(databasePath, "physical_extents", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "payload_id"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "version_id"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "is_delete_marker"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "cache_control"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "content_disposition"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "content_encoding"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "content_language"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "expires_at"));
  EXPECT_TRUE(tableHasColumn(databasePath, "objects", "checksum_type"));
  EXPECT_FALSE(tableHasColumn(databasePath, "objects", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "part_number"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "offset"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "checksum_algorithm"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "checksum_value"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_parts", "checksum_type"));
  EXPECT_TRUE(tableHasColumn(databasePath, "buckets", "versioning_enabled"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_payloads", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_payloads", "internal_checksum_algorithm"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_payloads", "internal_checksum_value"));
  EXPECT_TRUE(tableHasColumn(databasePath, "object_payloads", "dedup_algorithm"));
  EXPECT_FALSE(tableHasColumn(databasePath, "multipart_uploads", "content_length"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "checksum_algorithm"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "checksum_type"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "cache_control"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "content_disposition"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "content_encoding"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "content_language"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_uploads", "expires_at"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_parts", "internal_checksum_algorithm"));
  EXPECT_TRUE(tableHasColumn(databasePath, "multipart_parts", "internal_checksum_value"));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM sqlite_master "
                                      "WHERE type = 'index' AND name = 'objects_payload_state_idx'"),
            1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM sqlite_master "
                                      "WHERE type = 'index' AND name = 'physical_extents_free_length_location_idx'"),
            1);
}

TEST(SqliteIndexSchemaTest, RequiresPersistentWalJournalMode)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));
    ASSERT_TRUE(succeeded(index.checkpoint()));
  }

  EXPECT_EQ(scalarTextQuery(databasePath, "PRAGMA journal_mode"), "wal");
  EXPECT_EQ(scalarQuery(databasePath, "PRAGMA user_version"), 1);
}

TEST(SqliteIndexSchemaTest, RejectsStorageThatCannotEnterWalMode)
{
  extora::core::sqlite_object_index index{":memory:", 16};

  const extora::storage_error error = index.open();

  EXPECT_EQ(error.code, extora::storage_error_code::index_failure);
}

} // namespace extoraTest
