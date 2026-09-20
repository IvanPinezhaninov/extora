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

#include <string>
#include <string_view>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

extora::storage_error putText(extora::object_store& store, std::string_view bucket, std::string_view key,
                              std::string_view text, const extora::put_object_options& options = {})
{
  VectorReader reader{bytesFromString(text), 7};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  extora::put_object_options effectiveOptions = options;
  effectiveOptions.expected_content_length = text.size();
  return store.put_object(extora::bucket_name{std::string{bucket}}, extora::object_key{std::string{key}}, reader,
                          metadata, ignoredPutResult(), effectiveOptions);
}

TEST_F(StoreCoreTest, DeletesEmptyBucketAndRejectsNonEmptyBucket)
{
  ASSERT_TRUE(succeeded(m_core->create_bucket(extora::bucket_name{"empty"})));
  EXPECT_TRUE(succeeded(m_core->delete_bucket(extora::bucket_name{"empty"})));

  extora::bucket_info info;
  EXPECT_EQ(m_core->head_bucket(extora::bucket_name{"empty"}, info).code, extora::storage_error_code::bucket_not_found);

  ASSERT_TRUE(succeeded(putText(*m_core, "photos", "object", "value")));
  EXPECT_EQ(m_core->delete_bucket(extora::bucket_name{"photos"}).code, extora::storage_error_code::bucket_not_empty);
  ASSERT_TRUE(succeeded(m_core->head_bucket(extora::bucket_name{"photos"}, info)));
  EXPECT_EQ(info.name.value, "photos");
  EXPECT_NE(info.created_at, std::chrono::system_clock::time_point{});
  EXPECT_EQ(info.versioning, extora::bucket_versioning_status::unversioned);
}

TEST_F(StoreCoreTest, ListsBucketsInLexicographicOrderWithMetadata)
{
  ASSERT_TRUE(succeeded(m_core->create_bucket(extora::bucket_name{"zeta"})));
  ASSERT_TRUE(succeeded(m_core->create_bucket(extora::bucket_name{"alpha"})));
  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"alpha"}, extora::bucket_versioning_configuration::enabled)));

  extora::bucket_list list;
  ASSERT_TRUE(succeeded(m_core->list_buckets(list)));
  ASSERT_EQ(list.buckets.size(), 3);
  EXPECT_EQ(list.buckets[0].name.value, "alpha");
  EXPECT_EQ(list.buckets[0].versioning, extora::bucket_versioning_status::enabled);
  EXPECT_NE(list.buckets[0].created_at, std::chrono::system_clock::time_point{});
  EXPECT_EQ(list.buckets[1].name.value, "photos");
  EXPECT_EQ(list.buckets[1].versioning, extora::bucket_versioning_status::unversioned);
  EXPECT_EQ(list.buckets[2].name.value, "zeta");
}

} // namespace

} // namespace extoraTest
