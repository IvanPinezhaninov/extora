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

TEST(SqliteIndexExtentStateTest, PersistsReservationBeforePublish)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 8);
}

TEST(SqliteIndexExtentStateTest, RepeatsOperationReservationIdempotently)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent first;
  ASSERT_TRUE(succeeded(index.reserve_extent("write-operation", 0, 8, first)));
  extora::core::physical_extent repeated;
  ASSERT_TRUE(succeeded(index.reserve_extent("write-operation", 0, 8, repeated)));

  EXPECT_EQ(repeated.segment_id, first.segment_id);
  EXPECT_EQ(repeated.offset, first.offset);
  EXPECT_EQ(repeated.length, first.length);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);

  extora::core::physical_extent next;
  ASSERT_TRUE(succeeded(index.reserve_extent("write-operation", 1, 8, next)));
  EXPECT_EQ(next.segment_id, first.segment_id);
  EXPECT_EQ(next.offset, first.offset + first.length);
}

TEST(SqliteIndexExtentStateTest, CommitsReservedExtentWhenObjectIsPublished)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
  extent.length = 5;

  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"object"};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  object.version_id.value = extora::null_version_id;
  object.metadata.content_length = 5;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "object-value"};
  object.payload.extents.push_back(extent);

  ASSERT_TRUE(succeeded(index.publish_object(object)));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 5);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 5), 3);

  extora::core::indexed_object metadataOnly;
  ASSERT_TRUE(
      succeeded(index.find_object_metadata(extora::bucket_name{"photos"}, extora::object_key{"object"}, metadataOnly)));
  ASSERT_TRUE(metadataOnly.metadata.content_length.has_value());
  EXPECT_EQ(*metadataOnly.metadata.content_length, 5);
  EXPECT_TRUE(metadataOnly.payload.extents.empty());
}

TEST(SqliteIndexExtentStateTest, AbandonsReservedExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
  extent.length = 3;
  const std::vector<extora::core::physical_extent> extents{extent};

  ASSERT_TRUE(succeeded(index.abandon_extents(extents)));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);

  extora::reclamation_estimate beforeReclamation;
  ASSERT_TRUE(succeeded(index.get_reclamation_estimate(beforeReclamation)));
  EXPECT_EQ(beforeReclamation.reclaimable_bytes, 8);
  EXPECT_EQ(beforeReclamation.reclaimable_extent_count, 1);

  extora::core::storage_reclamation_plan plan;
  extora::reclaim_storage_result result;
  ASSERT_TRUE(succeeded(index.prepare_reclamation({}, {}, plan, result)));
  EXPECT_EQ(result.reclaimed_bytes, 8u);
  EXPECT_EQ(result.reclaimed_extent_count, 1u);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReleasing), 1);
  ASSERT_TRUE(succeeded(index.finish_reclamation(plan)));
  extora::reclamation_estimate afterReclamation;
  ASSERT_TRUE(succeeded(index.get_reclamation_estimate(afterReclamation)));
  EXPECT_EQ(afterReclamation.reclaimable_bytes, 0);
  EXPECT_EQ(afterReclamation.reclaimable_extent_count, 0);
}

TEST(SqliteIndexExtentStateTest, PartiallyReplacesPayloadExtentManifest)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

  extora::core::physical_extent first;
  extora::core::physical_extent second;
  ASSERT_TRUE(succeeded(index.reserve_extent("object", 0, 4, first)));
  ASSERT_TRUE(succeeded(index.reserve_extent("object", 1, 4, second)));

  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"object"};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  object.version_id.value = extora::null_version_id;
  object.metadata.content_length = 8;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "object-value"};
  object.payload.extents = {first, second};
  ASSERT_TRUE(succeeded(index.publish_object(object)));

  extora::core::indexed_object stored;
  ASSERT_TRUE(succeeded(index.find_object(object.bucket, object.key, stored)));
  extora::core::storage_compaction_layout layout;
  ASSERT_TRUE(succeeded(index.prepare_compaction(layout)));
  ASSERT_EQ(layout.extents.size(), 3u);
  EXPECT_EQ(layout.extents[0].kind, extora::core::compaction_extent_kind::movable);
  EXPECT_EQ(layout.extents[1].kind, extora::core::compaction_extent_kind::movable);
  EXPECT_EQ(layout.extents[2].kind, extora::core::compaction_extent_kind::reusable);

  const extora::core::physical_extent replacement{1, 8, 4, 4};
  ASSERT_TRUE(succeeded(index.reserve_extent_at("compaction", 0, replacement)));
  bool replaced = false;
  ASSERT_TRUE(succeeded(
      index.replace_payload_extents(stored.payload.id, stored.payload.extents, {first, replacement}, replaced)));
  ASSERT_TRUE(replaced);

  extora::core::indexed_object compacted;
  ASSERT_TRUE(succeeded(index.find_object(object.bucket, object.key, compacted)));
  ASSERT_EQ(compacted.payload.extents.size(), 2u);
  EXPECT_EQ(compacted.payload.extents[0].offset, first.offset);
  EXPECT_EQ(compacted.payload.extents[1].offset, replacement.offset);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 2);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

} // namespace extoraTest
