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
#include <cstddef>
#include <string>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(StoreCoreTest, ListsEmptyBucket)
{
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result)));

  EXPECT_TRUE(result.objects.empty());
  EXPECT_FALSE(result.is_truncated);
  EXPECT_FALSE(result.next_continuation_token.has_value());
}

TEST_F(StoreCoreTest, ListsObjectsInLexicographicOrder)
{
  const std::vector<std::string> keys{"zeta", "alpha", "alpha/child", "beta"};
  for (const std::string& key : keys) {
    VectorReader reader{bytesFromString(key), 16};
    ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{key}, reader,
                                             extora::object_metadata{}, ignoredPutResult())));
  }

  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result)));

  ASSERT_EQ(result.objects.size(), 4);
  EXPECT_EQ(result.objects[0].key.value, "alpha");
  EXPECT_EQ(result.objects[1].key.value, "alpha/child");
  EXPECT_EQ(result.objects[2].key.value, "beta");
  EXPECT_EQ(result.objects[3].key.value, "zeta");
  EXPECT_FALSE(result.is_truncated);
  EXPECT_FALSE(result.next_continuation_token.has_value());
}

TEST_F(StoreCoreTest, ListsObjectsByPrefix)
{
  const std::vector<std::string> keys{"photos/2026/a.jpg", "photos/2026/b.jpg", "photos/2025/a.jpg", "notes.txt"};
  for (const std::string& key : keys) {
    VectorReader reader{bytesFromString(key), 16};
    ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{key}, reader,
                                             extora::object_metadata{}, ignoredPutResult())));
  }

  extora::list_objects_options options;
  options.prefix = "photos/2026/";

  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result, options)));

  ASSERT_EQ(result.objects.size(), 2);
  EXPECT_EQ(result.objects[0].key.value, "photos/2026/a.jpg");
  EXPECT_EQ(result.objects[1].key.value, "photos/2026/b.jpg");
  EXPECT_FALSE(result.is_truncated);
}

TEST_F(StoreCoreTest, ListsObjectsWithContinuationToken)
{
  const std::vector<std::string> keys{"a", "b", "c"};
  for (const std::string& key : keys) {
    VectorReader reader{bytesFromString(key), 16};
    ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{key}, reader,
                                             extora::object_metadata{}, ignoredPutResult())));
  }

  extora::list_objects_options firstOptions;
  firstOptions.max_keys = 2;

  extora::object_list firstResult;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, firstResult, firstOptions)));
  ASSERT_EQ(firstResult.objects.size(), 2);
  EXPECT_EQ(firstResult.objects[0].key.value, "a");
  EXPECT_EQ(firstResult.objects[1].key.value, "b");
  EXPECT_TRUE(firstResult.is_truncated);
  ASSERT_TRUE(firstResult.next_continuation_token.has_value());
  EXPECT_NE(*firstResult.next_continuation_token, "b");

  extora::list_objects_options secondOptions;
  secondOptions.continuation_token = *firstResult.next_continuation_token;

  extora::object_list secondResult;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, secondResult, secondOptions)));
  ASSERT_EQ(secondResult.objects.size(), 1);
  EXPECT_EQ(secondResult.objects[0].key.value, "c");
  EXPECT_FALSE(secondResult.is_truncated);
  EXPECT_FALSE(secondResult.next_continuation_token.has_value());
}

TEST_F(StoreCoreTest, CapsObjectAndVersionPagesAtTheS3Maximum)
{
  for (std::size_t index = 0; index <= extora::max_list_page_size; ++index) {
    extora::core::indexed_object object;
    object.bucket = extora::bucket_name{"photos"};
    object.key = extora::object_key{"object-" + std::to_string(index)};
    object.metadata.content_length = 0;
    object.payload.internal_checksum =
        extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "empty-value"};
    object.etag = "empty-etag";
    object.version_id = extora::object_version_id{extora::null_version_id};
    object.created_at = std::chrono::system_clock::now();
    object.modified_at = object.created_at;
    ASSERT_TRUE(succeeded(m_index->publish_object(object)));
  }

  extora::list_objects_options options;
  options.max_keys = extora::max_list_page_size + 100;
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result, options)));
  EXPECT_EQ(result.objects.size(), extora::max_list_page_size);
  EXPECT_TRUE(result.is_truncated);
  EXPECT_TRUE(result.next_continuation_token.has_value());

  extora::list_object_versions_options versionOptions;
  versionOptions.max_keys = extora::max_list_page_size + 100;
  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions, versionOptions)));
  EXPECT_EQ(versions.versions.size(), extora::max_list_page_size);
  EXPECT_TRUE(versions.is_truncated);
  EXPECT_TRUE(versions.next_key_marker.has_value());
  EXPECT_TRUE(versions.next_version_id_marker.has_value());
}

TEST_F(StoreCoreTest, ListObjectsHidesDeletedAndOldGenerations)
{
  VectorReader oldReader{bytesFromString("old"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"current"}, oldReader,
                                           extora::object_metadata{}, ignoredPutResult())));

  VectorReader newReader{bytesFromString("new"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"current"}, newReader,
                                           extora::object_metadata{}, ignoredPutResult())));

  VectorReader deletedReader{bytesFromString("deleted"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"deleted"}, deletedReader,
                                           extora::object_metadata{}, ignoredPutResult())));
  ASSERT_TRUE(succeeded(
      m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"deleted"}, ignoredDeleteResult())));

  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result)));

  ASSERT_EQ(result.objects.size(), 1);
  EXPECT_EQ(result.objects[0].key.value, "current");
  EXPECT_EQ(result.objects[0].content_length, 3);
}

} // namespace extoraTest
