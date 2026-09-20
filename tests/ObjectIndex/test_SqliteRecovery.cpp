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

#include <chrono>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

namespace {

extora::core::indexed_object makeObject(std::string checksum, extora::core::physical_extent extent)
{
  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"object"};
  object.metadata.content_length = extent.length;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, std::move(checksum)};
  object.payload.extents.push_back(extent);
  object.etag = object.payload.internal_checksum.value;
  object.version_id.value = extora::null_version_id;
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  return object;
}

} // namespace

TEST(SqliteIndexRecoveryTest, NormalizesUnpublishedReservationsOnOpen)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent storage;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, storage)));
  }

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

TEST(SqliteIndexRecoveryTest, MakesAnInterruptedPhysicalReleaseReusableOnOpen)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
    ASSERT_TRUE(succeeded(index.abandon_extents({extent})));

    extora::core::storage_reclamation_plan plan;
    extora::reclaim_storage_result result;
    ASSERT_TRUE(succeeded(index.prepare_reclamation({}, {}, plan, result)));
    ASSERT_EQ(plan.extents.size(), 1u);
    EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReleasing), 1);
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReleasing), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 8, reused)));
  EXPECT_EQ(reused.segment_id, 1u);
  EXPECT_EQ(reused.offset, 0u);
}

TEST(SqliteIndexRecoveryTest, QuarantinesCurrentObjectWithReservedExtentOnOpen)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));
    ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));
  }

  const std::string sql = "INSERT INTO physical_extents "
                          "(segment_id, offset, length, state) "
                          "VALUES (1, 0, 8, " +
                          std::to_string(sqliteExtentStateReserved) +
                          "); "
                          "INSERT INTO object_payloads "
                          "(content_length, internal_checksum_algorithm, internal_checksum_value) "
                          "VALUES (0, 'xxh3-128', 'invalid'); "
                          "INSERT INTO objects "
                          "(bucket_id, payload_id, key, etag, version_id, is_delete_marker, "
                          "created_at, modified_at, generation, state) "
                          "VALUES ((SELECT id FROM buckets WHERE name = 'photos'), "
                          "(SELECT id FROM object_payloads), 'partial', 'partial-etag', 'partial-version', 0, "
                          "1, 1, 1, " +
                          std::to_string(sqliteObjectStateCurrent) +
                          "); "
                          "INSERT INTO payload_extents (payload_id, ordinal, physical_extent_id) "
                          "VALUES ((SELECT payload_id FROM objects WHERE key = 'partial'), 0, "
                          "(SELECT id FROM physical_extents WHERE segment_id = 1 AND offset = 0));";
  ASSERT_TRUE(executeSql(databasePath, sql));

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(countObjectExtentLinksInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCorrupted), 1);

  extora::core::indexed_object object;
  ASSERT_TRUE(
      succeeded(recoveredIndex.find_object(extora::bucket_name{"photos"}, extora::object_key{"partial"}, object)));
  EXPECT_TRUE(object.is_corrupted);
}

TEST(SqliteIndexRecoveryTest, NormalizesGarbageExtentsWithoutLeavingPayloadLinks)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 1024};
    ASSERT_TRUE(succeeded(index.open()));
    ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

    extora::core::physical_extent firstExtent;
    ASSERT_TRUE(succeeded(index.reserve_extent("first", 0, 5, firstExtent)));
    firstExtent.length = 5;
    ASSERT_TRUE(succeeded(index.publish_object(makeObject("first", firstExtent))));

    extora::core::physical_extent secondExtent;
    ASSERT_TRUE(succeeded(index.reserve_extent("second", 0, 6, secondExtent)));
    secondExtent.length = 6;
    ASSERT_TRUE(succeeded(index.publish_object(makeObject("second", secondExtent))));
  }

  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 1);

  extora::core::sqlite_object_index recoveredIndex{databasePath, 1024};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(countObjectExtentLinksInState(databasePath, sqliteExtentStateFree), 0);
}

} // namespace extoraTest
