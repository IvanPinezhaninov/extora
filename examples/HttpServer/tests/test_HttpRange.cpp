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

#include <optional>

#include <gtest/gtest.h>

#include "HttpRange.h"

namespace extoraHttpExample {

TEST(HttpRangeTest, ParsesEverySupportedRangeForm)
{
  extora::byte_range range;
  ASSERT_TRUE(extora::succeeded(parseByteRange("bytes=10-19", range)));
  EXPECT_EQ(range.type, extora::byte_range_type::offset_length);
  EXPECT_EQ(range.offset, 10u);
  EXPECT_EQ(range.length, 10u);

  ASSERT_TRUE(extora::succeeded(parseByteRange("bytes=10-", range)));
  EXPECT_EQ(range.type, extora::byte_range_type::offset_to_end);
  EXPECT_EQ(range.offset, 10u);

  ASSERT_TRUE(extora::succeeded(parseByteRange("bytes=-20", range)));
  EXPECT_EQ(range.type, extora::byte_range_type::suffix);
  EXPECT_EQ(range.length, 20u);
}

TEST(HttpRangeTest, RejectsMalformedAndMultipleRanges)
{
  extora::byte_range range;
  EXPECT_EQ(parseByteRange("items=1-2", range).code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(parseByteRange("bytes=20-10", range).code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(parseByteRange("bytes=-0", range).code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(parseByteRange("bytes=0-1,4-5", range).code, extora::storage_error_code::invalid_range);
}

TEST(HttpRangeTest, ParsesAnOptionalRangeWithoutObjectMetadata)
{
  boost::beast::http::fields fields;
  std::optional<extora::byte_range> range = extora::byte_range{};
  ASSERT_TRUE(extora::succeeded(parseRequestedRange(fields, range)));
  EXPECT_FALSE(range.has_value());

  fields.set(boost::beast::http::field::range, "bytes=8-");
  ASSERT_TRUE(extora::succeeded(parseRequestedRange(fields, range)));
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->type, extora::byte_range_type::offset_to_end);
  EXPECT_EQ(range->offset, 8u);
}

TEST(HttpRangeTest, ResolvesAndClampsRequestedRanges)
{
  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::range, "bytes=8-20");
  std::optional<extora::byte_range> range;
  std::optional<extora::resolved_byte_range> resolved;
  ASSERT_TRUE(extora::succeeded(parseRange(fields, 10, range, resolved)));
  ASSERT_TRUE(range.has_value());
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(range->offset, 8u);
  EXPECT_EQ(range->length, 2u);
  EXPECT_EQ(resolved->offset, 8u);
  EXPECT_EQ(resolved->length, 2u);

  fields.set(boost::beast::http::field::range, "bytes=-20");
  ASSERT_TRUE(extora::succeeded(parseRange(fields, 10, range, resolved)));
  EXPECT_EQ(resolved->offset, 0u);
  EXPECT_EQ(resolved->length, 10u);
}

TEST(HttpRangeTest, RejectsRangesOutsideAnObject)
{
  boost::beast::http::fields fields;
  fields.set(boost::beast::http::field::range, "bytes=10-");
  std::optional<extora::byte_range> range;
  std::optional<extora::resolved_byte_range> resolved;
  EXPECT_EQ(parseRange(fields, 10, range, resolved).code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(parseRange(fields, 0, range, resolved).code, extora::storage_error_code::invalid_range);
}

} // namespace extoraHttpExample
