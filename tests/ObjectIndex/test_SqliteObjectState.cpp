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

TEST(SqliteIndexObjectStateTest, DistinguishesSupersededAndDeletedObjects)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 1024};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));
  ASSERT_TRUE(
      succeeded(index.set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_status::enabled)));

  extora::core::indexed_object first;
  first.bucket = extora::bucket_name{"photos"};
  first.key = extora::object_key{"image.bin"};
  first.created_at = std::chrono::system_clock::now();
  first.modified_at = first.created_at;
  first.metadata.content_length = 4;
  first.version_id.value = "first-version";
  first.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "first-value"};

  extora::core::physical_extent firstStorage;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, firstStorage)));

  extora::core::physical_extent firstExtent = firstStorage;
  firstExtent.length = 4;
  first.payload.extents.push_back(firstExtent);

  ASSERT_TRUE(succeeded(index.publish_object(first)));

  extora::core::indexed_object second = first;
  second.version_id.value = "second-version";
  second.payload.internal_checksum.value = "second-value";
  second.created_at = std::chrono::system_clock::now();
  second.modified_at = second.created_at;
  ASSERT_TRUE(succeeded(reserveTestExtent(index, 4, second.payload.extents[0])));
  second.payload.extents[0].length = 4;

  ASSERT_TRUE(succeeded(index.publish_object(second)));

  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCurrent), 1);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateSuperseded), 1);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateDeleted), 0);

  extora::delete_object_options deleteFirst;
  deleteFirst.version_id = first.version_id;
  ASSERT_TRUE(succeeded(index.delete_object(extora::bucket_name{"photos"}, extora::object_key{"image.bin"}, deleteFirst,
                                            extora::object_version_id{"test-delete"}, ignoredDeleteResult())));

  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCurrent), 1);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateSuperseded), 0);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateDeleted), 1);
}

} // namespace extoraTest
