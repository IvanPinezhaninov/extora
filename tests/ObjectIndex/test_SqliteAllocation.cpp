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

#include <limits>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

TEST(SqliteIndexAllocatorTest, ReservesSegmentTailBeforeMovingToNextSegment)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent first;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 10, first)));
  EXPECT_EQ(first.segment_id, 1);
  EXPECT_EQ(first.offset, 0);
  EXPECT_EQ(first.length, 10);

  extora::core::physical_extent tail;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 10, tail)));
  EXPECT_EQ(tail.segment_id, 1);
  EXPECT_EQ(tail.offset, 10);
  EXPECT_EQ(tail.length, 6);

  extora::core::physical_extent next;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 10, next)));
  EXPECT_EQ(next.segment_id, 2);
  EXPECT_EQ(next.offset, 0);
  EXPECT_EQ(next.length, 10);
}

TEST(SqliteIndexAllocatorTest, ReservesSequentialExtentsAcrossSegments)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 4};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent first;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, first)));
  EXPECT_EQ(first.segment_id, 1);
  EXPECT_EQ(first.offset, 0);
  EXPECT_EQ(first.length, 4);

  extora::core::physical_extent second;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, second)));
  EXPECT_EQ(second.segment_id, 2);
  EXPECT_EQ(second.offset, 0);
  EXPECT_EQ(second.length, 4);

  extora::core::physical_extent third;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 2, third)));
  EXPECT_EQ(third.segment_id, 3);
  EXPECT_EQ(third.offset, 0);
  EXPECT_EQ(third.length, 2);
}

TEST(SqliteIndexAllocatorTest, ContiguousReservationSkipsSegmentTail)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent first;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 10, first)));

  extora::core::physical_extent contiguous;
  ASSERT_TRUE(succeeded(
      index.reserve_extent("contiguous", 0, 10, contiguous, extora::core::extent_allocation_mode::contiguous)));
  EXPECT_EQ(contiguous.segment_id, 2);
  EXPECT_EQ(contiguous.offset, 0);
  EXPECT_EQ(contiguous.length, 10);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 10), 6);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

TEST(SqliteIndexAllocatorTest, ContiguousReservationDoesNotUseSmallerFreeExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));
    extora::core::physical_extent small;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, small)));
    ASSERT_TRUE(succeeded(index.abandon_extents(std::vector<extora::core::physical_extent>{small})));
  }

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::physical_extent contiguous;
  ASSERT_TRUE(succeeded(
      index.reserve_extent("contiguous", 0, 8, contiguous, extora::core::extent_allocation_mode::contiguous)));
  EXPECT_EQ(contiguous.segment_id, 1);
  EXPECT_EQ(contiguous.offset, 4);
  EXPECT_EQ(contiguous.length, 8);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 4);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

TEST(SqliteIndexAllocatorTest, ReleasesUnusedMultipartReservationTail)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 16};
  ASSERT_TRUE(succeeded(index.open()));

  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"object"};
  ASSERT_TRUE(succeeded(index.create_bucket(bucket)));

  extora::core::indexed_multipart_upload upload;
  upload.upload_id = extora::multipart_upload_id{"upload"};
  upload.bucket = bucket;
  upload.key = key;
  upload.initiated_at = std::chrono::system_clock::now();
  ASSERT_TRUE(succeeded(index.create_multipart_upload(upload)));

  extora::core::physical_extent extent;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
  extent.length = 4;

  extora::core::indexed_multipart_part part;
  part.info.part_number = 1;
  part.info.etag = "part-etag";
  part.info.content_length = 4;
  part.info.created_at = std::chrono::system_clock::now();
  part.internal_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "part-value"};
  part.extents.push_back(extent);
  ASSERT_TRUE(succeeded(index.store_multipart_part(upload.upload_id, bucket, key, part)));

  extora::core::physical_extent tail;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, tail)));
  EXPECT_EQ(tail.segment_id, extent.segment_id);
  EXPECT_EQ(tail.offset, extent.offset + extent.length);
  EXPECT_EQ(tail.length, 4u);
}

TEST(SqliteIndexAllocatorTest, RejectsIncompatibleSegmentCapacityOnOpen)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 12, extent)));
  }

  extora::core::sqlite_object_index index{databasePath, 8};
  const extora::storage_error error = index.open();
  EXPECT_EQ(error.code, extora::storage_error_code::index_failure);
}

TEST(SqliteIndexAllocatorTest, RejectsValuesOutsideSqliteIntegerRange)
{
  const std::string root = makeTempRoot();
  const std::uint64_t oversized = std::numeric_limits<std::uint64_t>::max();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), oversized};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::physical_extent extent;
  const extora::storage_error error = reserveTestExtent(index, oversized, extent);

  EXPECT_EQ(error.code, extora::storage_error_code::index_failure);
}

TEST(SqliteIndexAllocatorTest, ReusesExactSizeFreeExtentBeforeAppending)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
    const std::vector<extora::core::physical_extent> extents{extent};
    ASSERT_TRUE(succeeded(index.abandon_extents(extents)));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 8, reused)));
  EXPECT_EQ(reused.segment_id, 1);
  EXPECT_EQ(reused.offset, 0);
  EXPECT_EQ(reused.length, 8);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);
}

TEST(SqliteIndexAllocatorTest, SplitsSmallestLargerFreeExtentAndReusesRemainder)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
    const std::vector<extora::core::physical_extent> extents{extent};
    ASSERT_TRUE(succeeded(index.abandon_extents(extents)));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));

  extora::core::physical_extent firstHalf;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 4, firstHalf)));
  EXPECT_EQ(firstHalf.segment_id, 1);
  EXPECT_EQ(firstHalf.offset, 0);
  EXPECT_EQ(firstHalf.length, 4);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 4), 4);

  extora::core::physical_extent secondHalf;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 4, secondHalf)));
  EXPECT_EQ(secondHalf.segment_id, 1);
  EXPECT_EQ(secondHalf.offset, 4);
  EXPECT_EQ(secondHalf.length, 4);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 2);
}

TEST(SqliteIndexAllocatorTest, ReusesLargestSmallerFreeExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, extent)));
    const std::vector<extora::core::physical_extent> extents{extent};
    ASSERT_TRUE(succeeded(index.abandon_extents(extents)));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 8, reused)));
  EXPECT_EQ(reused.segment_id, 1);
  EXPECT_EQ(reused.offset, 0);
  EXPECT_EQ(reused.length, 4);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);
}

TEST(SqliteIndexAllocatorTest, SelectsSmallestLargerFreeExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 32};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(executeSql(databasePath, R"sql(
    INSERT INTO physical_extents
      (segment_id, offset, length, state)
    VALUES
      (1, 0, 12, 6),
      (1, 16, 8, 6)
    )sql"));

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 6, reused)));
  EXPECT_EQ(reused.segment_id, 1);
  EXPECT_EQ(reused.offset, 16);
  EXPECT_EQ(reused.length, 6);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 12);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 22), 2);
}

TEST(SqliteIndexAllocatorTest, SelectsLargestSmallerFreeExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 32};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(executeSql(databasePath, R"sql(
    INSERT INTO physical_extents
      (segment_id, offset, length, state)
    VALUES
      (1, 0, 4, 6),
      (1, 8, 6, 6)
    )sql"));

  extora::core::physical_extent reused;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, reused)));
  EXPECT_EQ(reused.segment_id, 1);
  EXPECT_EQ(reused.offset, 8);
  EXPECT_EQ(reused.length, 6);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 4);
}

TEST(SqliteIndexAllocatorTest, CoalescesAdjacentFreeExtentsOnOpen)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    std::vector<extora::core::physical_extent> extents;
    for (std::uint64_t ordinal = 0; ordinal < 3; ++ordinal) {
      extora::core::physical_extent extent;
      ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, extent)));
      extents.push_back(extent);
    }
    ASSERT_TRUE(succeeded(index.abandon_extents(extents)));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 12);
}

} // namespace extoraTest
