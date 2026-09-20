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

#include "extora/core/object_lookup_cache.h"

#include <gtest/gtest.h>

namespace extoraTest {

namespace {

extora::core::indexed_object makeIndexedObject(const char* bucket, const char* key, std::uint64_t length)
{
  extora::core::indexed_object object;
  object.bucket.value = bucket;
  object.key.value = key;
  object.metadata.content_length = length;

  extora::core::physical_extent extent;
  extent.length = length;
  object.payload.extents.push_back(extent);
  return object;
}

} // namespace

TEST(ObjectLookupCache, RetainsMostRecentlyUsedEntries)
{
  extora::core::object_lookup_cache cache{2};
  cache.insert(makeIndexedObject("photos", "first", 1));
  cache.insert(makeIndexedObject("photos", "second", 2));

  extora::core::indexed_object result;
  ASSERT_TRUE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"first"}, result));
  cache.insert(makeIndexedObject("photos", "third", 3));

  EXPECT_FALSE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"second"}, result));
  EXPECT_TRUE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"first"}, result));
  EXPECT_TRUE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"third"}, result));
}

TEST(ObjectLookupCache, ReplacesAndErasesEntries)
{
  extora::core::object_lookup_cache cache{1};
  cache.insert(makeIndexedObject("photos", "object", 1));
  cache.insert(makeIndexedObject("photos", "object", 7));

  extora::core::indexed_object result;
  ASSERT_TRUE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"object"}, result));
  ASSERT_TRUE(result.metadata.content_length.has_value());
  EXPECT_EQ(*result.metadata.content_length, 7);

  cache.erase(extora::bucket_name{"photos"}, extora::object_key{"object"});
  EXPECT_FALSE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"object"}, result));
}

TEST(ObjectLookupCache, ZeroCapacityDisablesStorage)
{
  extora::core::object_lookup_cache cache{0};
  cache.insert(makeIndexedObject("photos", "object", 1));

  extora::core::indexed_object result;
  EXPECT_FALSE(cache.find(extora::bucket_name{"photos"}, extora::object_key{"object"}, result));
}

} // namespace extoraTest
