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

#include <string_view>

#include "ApiTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, ListsAnEmptyBucketAndObjectsInLexicographicOrder)
{
  extora::object_list empty;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, empty)));
  EXPECT_TRUE(empty.objects.empty());

  for (std::string_view key : {"zeta", "alpha", "middle"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::object_list objects;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, objects)));
  ASSERT_EQ(objects.objects.size(), 3u);
  EXPECT_EQ(objects.objects[0].key.value, "alpha");
  EXPECT_EQ(objects.objects[1].key.value, "middle");
  EXPECT_EQ(objects.objects[2].key.value, "zeta");
}

TEST_F(ObjectStoreApiTest, IncludesCustomMetadataInObjectListings)
{
  extora::object_metadata metadata;
  metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "api-test"});
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "metadata", "body", metadata)));

  extora::object_list result;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, result)));
  ASSERT_EQ(result.objects.size(), 1u);
  ASSERT_EQ(result.objects[0].custom_metadata.size(), 1u);
  EXPECT_EQ(result.objects[0].custom_metadata[0].name, metadata.custom_metadata[0].name);
  EXPECT_EQ(result.objects[0].custom_metadata[0].value, metadata.custom_metadata[0].value);
}

TEST_F(ObjectStoreApiTest, FiltersByPrefixAndStartAfter)
{
  for (std::string_view key : {"images/a", "images/b", "images/c", "notes/a"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::list_objects_options options;
  options.prefix = "images/";
  options.start_after = "images/a";
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, result, options)));
  ASSERT_EQ(result.objects.size(), 2u);
  EXPECT_EQ(result.objects[0].key.value, "images/b");
  EXPECT_EQ(result.objects[1].key.value, "images/c");
}

TEST_F(ObjectStoreApiTest, GroupsCommonPrefixesWithADelimiter)
{
  for (std::string_view key : {"a/1", "a/2", "a/b/1", "a/c/1", "z/1"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::list_objects_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, result, options)));
  ASSERT_EQ(result.objects.size(), 2u);
  EXPECT_EQ(result.common_prefixes, (std::vector<std::string>{"a/b/", "a/c/"}));
}

TEST_F(ObjectStoreApiTest, FiltersCommonPrefixesAtStartAfter)
{
  for (std::string_view key : {"a/b/first", "a/b/zeta", "a/c/first"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::list_objects_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  options.start_after = "a/b/middle";
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, result, options)));
  EXPECT_TRUE(result.objects.empty());
  EXPECT_EQ(result.common_prefixes, (std::vector<std::string>{"a/c/"}));
}

TEST_F(ObjectStoreApiTest, ContinuesAPaginatedListing)
{
  for (std::string_view key : {"a", "b", "c"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::list_objects_options options;
  options.max_keys = 2;
  extora::object_list first;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, first, options)));
  ASSERT_EQ(first.objects.size(), 2u);
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_continuation_token.has_value());

  options.continuation_token = *first.next_continuation_token;
  extora::object_list second;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"photos"}, second, options)));
  ASSERT_EQ(second.objects.size(), 1u);
  EXPECT_EQ(second.objects[0].key.value, "c");
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_continuation_token.has_value());
}

TEST_F(ObjectStoreApiTest, RejectsInvalidObjectContinuationTokens)
{
  extora::list_objects_options options;
  options.continuation_token = "not-a-token";
  extora::object_list result;

  EXPECT_EQ(m_store->list_objects(extora::bucket_name{"photos"}, result, options).code,
            extora::storage_error_code::invalid_continuation_token);
}

} // namespace extoraTest
