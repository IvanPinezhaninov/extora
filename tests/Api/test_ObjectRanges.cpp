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

TEST_F(ObjectStoreApiTest, ReadsAnOffsetLengthRange)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "range", "0123456789")));
  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 3, 4};
  extora::open_object_result result;
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "range", text, options, &result)));
  EXPECT_EQ(text, "3456");
  ASSERT_TRUE(result.byte_range.has_value());
  EXPECT_EQ(result.byte_range->offset, 3u);
  EXPECT_EQ(result.byte_range->length, 4u);
}

TEST_F(ObjectStoreApiTest, ReadsFromAnOffsetToTheEnd)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "range", "0123456789")));
  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_to_end, 6, 0};
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "range", text, options)));
  EXPECT_EQ(text, "6789");
}

TEST_F(ObjectStoreApiTest, ReadsSuffixRangesAndClampsAnOversizedSuffix)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "range", "0123456789")));
  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::suffix, 0, 3};
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "range", text, options)));
  EXPECT_EQ(text, "789");

  options.range = extora::byte_range{extora::byte_range_type::suffix, 0, 100};
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "range", text, options)));
  EXPECT_EQ(text, "0123456789");
}

TEST_F(ObjectStoreApiTest, SupportsZeroLengthRangesIncludingEmptyObjects)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "range", "0123")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "empty", "")));
  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 4, 0};
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "range", text, options)));
  EXPECT_TRUE(text.empty());

  options.range = extora::byte_range{extora::byte_range_type::suffix, 0, 0};
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "empty", text, options)));
  EXPECT_TRUE(text.empty());
}

TEST_F(ObjectStoreApiTest, RejectsRangesOutsideTheObject)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "range", "0123")));
  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 3, 2};
  std::string text;
  extora::open_object_result result;
  EXPECT_EQ(readText(*m_store, "photos", "range", text, options, &result).code,
            extora::storage_error_code::invalid_range);
  EXPECT_EQ(result.object.content_length, 4u);
  EXPECT_EQ(result.reader, nullptr);

  options.range = extora::byte_range{extora::byte_range_type::offset_length, 5, 0};
  EXPECT_EQ(readText(*m_store, "photos", "range", text, options, &result).code,
            extora::storage_error_code::invalid_range);
  EXPECT_EQ(result.object.content_length, 4u);

  options.range = extora::byte_range{extora::byte_range_type::offset_to_end, 4, 0};
  EXPECT_EQ(readText(*m_store, "photos", "range", text, options, &result).code,
            extora::storage_error_code::invalid_range);
  EXPECT_EQ(result.object.content_length, 4u);
}

} // namespace extoraTest
