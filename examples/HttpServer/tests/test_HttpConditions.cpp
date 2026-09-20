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

#include <gtest/gtest.h>

#include "HttpConditions.h"

namespace extoraHttpExample {

TEST(HttpConditionsTest, ParsesStandardAndCustomMetadata)
{
  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::content_type, "image/jpeg");
  fields.set(boost::beast::http::field::cache_control, "max-age=60");
  fields.set(boost::beast::http::field::expires, "Thu, 01 Jan 1970 00:00:00 GMT");
  fields.set("X-Extora-Meta-Owner", "Alice");

  std::string errorMessage;
  extora::object_metadata metadata;
  ASSERT_TRUE(parseObjectMetadata(fields, metadata, errorMessage));
  EXPECT_EQ(metadata.content_type, "image/jpeg");
  EXPECT_EQ(metadata.cache_control, "max-age=60");
  ASSERT_TRUE(metadata.expires_at.has_value());
  ASSERT_EQ(metadata.custom_metadata.size(), 1u);
  EXPECT_EQ(metadata.custom_metadata[0].name, "Owner");
  EXPECT_EQ(metadata.custom_metadata[0].value, "Alice");
}

TEST(HttpConditionsTest, RejectsMalformedMetadata)
{
  std::string errorMessage;
  extora::object_metadata metadata;
  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::expires, "tomorrow");
  EXPECT_FALSE(parseObjectMetadata(fields, metadata, errorMessage));

  fields.clear();
  fields.set("X-Extora-Meta-", "value");
  EXPECT_FALSE(parseObjectMetadata(fields, metadata, errorMessage));
}

TEST(HttpConditionsTest, ParsesEtagsAndHttpDates)
{
  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::if_match, "\"etag-value\"");
  fields.set(boost::beast::http::field::if_none_match, "*");
  fields.set(boost::beast::http::field::if_modified_since, "Thu, 01 Jan 1970 00:00:00 GMT");

  std::string errorMessage;
  extora::object_conditions conditions;
  ASSERT_TRUE(parseObjectConditions(fields, "", conditions, errorMessage));
  EXPECT_EQ(conditions.if_match_etag, "etag-value");
  EXPECT_EQ(conditions.if_none_match_etag, extora::etag_wildcard);
  EXPECT_TRUE(conditions.if_modified_since.has_value());

  fields.set(boost::beast::http::field::if_match, "weak");
  EXPECT_FALSE(parseObjectConditions(fields, "", conditions, errorMessage));
}

TEST(HttpConditionsTest, EvaluatesConditionalReads)
{
  extora::object_info object;
  object.etag = "etag-value";
  object.modified_at = std::chrono::system_clock::time_point{};

  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::if_match, "\"other\"");
  EXPECT_EQ(checkObjectConditions(fields, object).code, extora::storage_error_code::precondition_failed);

  fields.clear();
  fields.set(boost::beast::http::field::if_none_match, "\"etag-value\"");
  EXPECT_EQ(checkObjectConditions(fields, object).code, extora::storage_error_code::not_modified);

  fields.set(boost::beast::http::field::if_none_match, "\"other\"");
  EXPECT_TRUE(extora::succeeded(checkObjectConditions(fields, object)));
}

TEST(HttpConditionsTest, DetectsConditionalReadHeaders)
{
  boost::beast::http::fields fields;
  EXPECT_FALSE(hasObjectConditions(fields));

  fields.set(boost::beast::http::field::if_match, "\"etag\"");
  EXPECT_TRUE(hasObjectConditions(fields));
  fields.clear();

  fields.set(boost::beast::http::field::if_none_match, "\"etag\"");
  EXPECT_TRUE(hasObjectConditions(fields));
  fields.clear();

  fields.set(boost::beast::http::field::if_modified_since, "Thu, 01 Jan 1970 00:00:00 GMT");
  EXPECT_TRUE(hasObjectConditions(fields));
  fields.clear();

  fields.set(boost::beast::http::field::if_unmodified_since, "Thu, 01 Jan 1970 00:00:00 GMT");
  EXPECT_TRUE(hasObjectConditions(fields));
}

} // namespace extoraHttpExample
