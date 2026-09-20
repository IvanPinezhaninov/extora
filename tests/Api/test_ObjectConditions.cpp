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

#include "ApiTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, AppliesEtagConditionsToHeadAndGet)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "value")));
  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info)));

  extora::head_object_options headOptions;
  headOptions.conditions.if_match_etag = info.etag;
  EXPECT_TRUE(
      succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info, headOptions)));
  headOptions.conditions.if_match_etag = "wrong";
  EXPECT_EQ(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info, headOptions).code,
            extora::storage_error_code::precondition_failed);
  EXPECT_FALSE(info.etag.empty());

  extora::open_object_options getOptions;
  getOptions.conditions.if_none_match_etag = info.etag;
  std::string text;
  extora::open_object_result result;
  EXPECT_EQ(readText(*m_store, "photos", "object", text, getOptions, &result).code,
            extora::storage_error_code::not_modified);
  EXPECT_EQ(result.object.etag, info.etag);
  EXPECT_EQ(result.reader, nullptr);
}

TEST_F(ObjectStoreApiTest, AppliesMatchConditionsToPut)
{
  extora::put_object_result original;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "original", extora::object_metadata{},
                                extora::put_object_options{}, &original)));

  extora::put_object_options options;
  options.conditions.if_match_etag = "wrong";
  EXPECT_EQ(putText(*m_store, "photos", "object", "rejected", {}, options).code,
            extora::storage_error_code::precondition_failed);
  options.conditions.if_match_etag = original.etag;
  EXPECT_TRUE(succeeded(putText(*m_store, "photos", "object", "replacement", {}, options)));

  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "object", text)));
  EXPECT_EQ(text, "replacement");
}

TEST_F(ObjectStoreApiTest, AppliesIfNoneMatchWildcardToPut)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "original")));
  extora::put_object_options options;
  options.conditions.if_none_match_etag = extora::etag_wildcard;
  EXPECT_EQ(putText(*m_store, "photos", "object", "rejected", {}, options).code,
            extora::storage_error_code::precondition_failed);
  EXPECT_TRUE(succeeded(putText(*m_store, "photos", "absent", "created", {}, options)));
}

TEST_F(ObjectStoreApiTest, AppliesModificationTimeConditions)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "value")));
  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info)));

  extora::open_object_options options;
  options.conditions.if_modified_since = std::chrono::system_clock::now();
  std::string text;
  EXPECT_EQ(readText(*m_store, "photos", "object", text, options).code, extora::storage_error_code::not_modified);
  options.conditions = {};
  options.conditions.if_unmodified_since = info.modified_at - std::chrono::seconds{1};
  EXPECT_EQ(readText(*m_store, "photos", "object", text, options).code,
            extora::storage_error_code::precondition_failed);
}

TEST_F(ObjectStoreApiTest, RejectsModificationTimePutConditionsForMissingObjects)
{
  extora::put_object_options options;
  options.conditions.if_modified_since = std::chrono::system_clock::now();
  EXPECT_EQ(putText(*m_store, "photos", "missing", "value", extora::object_metadata{}, options).code,
            extora::storage_error_code::precondition_failed);

  options.conditions = {};
  options.conditions.if_unmodified_since = std::chrono::system_clock::now();
  EXPECT_EQ(putText(*m_store, "photos", "missing", "value", extora::object_metadata{}, options).code,
            extora::storage_error_code::precondition_failed);
}

} // namespace extoraTest
