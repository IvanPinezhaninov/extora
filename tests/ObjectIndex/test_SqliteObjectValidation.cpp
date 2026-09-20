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
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
** USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

namespace {

extora::core::indexed_object makeObject(std::string_view key)
{
  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{std::string{key}};
  object.metadata.content_length = 0;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "empty"};
  object.etag = "etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  return object;
}

} // namespace

TEST(SqliteObjectValidationTest, RejectsInvalidPublishedObjectShape)
{
  extora::core::sqlite_object_index index{joinPath(makeTempRoot(), "index.sqlite3"), 16};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

  extora::core::indexed_object missingLength = makeObject("missing-length");
  missingLength.metadata.content_length.reset();
  EXPECT_EQ(index.publish_object(missingLength).code, extora::storage_error_code::index_failure);

  extora::core::indexed_object missingExtent = makeObject("missing-extent");
  missingExtent.metadata.content_length = 1;
  EXPECT_EQ(index.publish_object(missingExtent).code, extora::storage_error_code::index_failure);

  extora::core::indexed_object missingVersion = makeObject("missing-version");
  missingVersion.version_id.value.clear();
  EXPECT_EQ(index.publish_object(missingVersion).code, extora::storage_error_code::index_failure);
}

TEST(SqliteObjectValidationTest, RejectsInvalidPayloadReferences)
{
  extora::core::sqlite_object_index index{joinPath(makeTempRoot(), "index.sqlite3"), 16};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

  extora::core::indexed_object oversizedPayload = makeObject("oversized-payload");
  oversizedPayload.payload.id = (std::numeric_limits<std::uint64_t>::max)();
  EXPECT_EQ(index.publish_object(oversizedPayload).code, extora::storage_error_code::index_failure);

  extora::core::indexed_object missingPayload = makeObject("missing-payload");
  missingPayload.payload.id = 42;
  EXPECT_EQ(index.publish_object(missingPayload).code, extora::storage_error_code::index_failure);
}

TEST(SqliteObjectValidationTest, RejectsInvalidCompletedPartManifest)
{
  extora::core::sqlite_object_index index{joinPath(makeTempRoot(), "index.sqlite3"), 16};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(extora::bucket_name{"photos"})));

  extora::core::indexed_object invalidPart = makeObject("invalid-part");
  invalidPart.parts.push_back(
      extora::object_part_info{0, 0, 0, extora::object_checksum{extora::checksum_algorithm_name{"sum"}, "value"}});
  EXPECT_EQ(index.publish_object(invalidPart).code, extora::storage_error_code::index_failure);

  extora::core::indexed_object mismatchingLength = makeObject("mismatching-length");
  mismatchingLength.parts.push_back(
      extora::object_part_info{1, 0, 1, extora::object_checksum{extora::checksum_algorithm_name{"sum"}, "value"}});
  EXPECT_EQ(index.publish_object(mismatchingLength).code, extora::storage_error_code::index_failure);
}

} // namespace extoraTest
