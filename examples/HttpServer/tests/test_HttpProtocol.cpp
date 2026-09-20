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
#include <optional>
#include <string>

#include <boost/beast/http/status.hpp>

#include <gtest/gtest.h>

#include "HttpProtocol.h"
#include "extora/storage_error.h"

namespace extoraHttpExample {

TEST(HttpProtocolTest, MapsStorageErrorsToStableNamesAndStatuses)
{
  EXPECT_STREQ(storageErrorCodeName(extora::storage_error_code::object_not_found), "object_not_found");
  EXPECT_EQ(statusForStorageError(extora::storage_error_code::object_not_found), boost::beast::http::status::not_found);
  EXPECT_EQ(statusForStorageError(extora::storage_error_code::invalid_range),
            boost::beast::http::status::range_not_satisfiable);
  EXPECT_STREQ(storageErrorCodeName(extora::storage_error_code::object_corrupted), "object_corrupted");
  EXPECT_EQ(statusForStorageError(extora::storage_error_code::object_corrupted),
            boost::beast::http::status::internal_server_error);
  EXPECT_EQ(statusForStorageError(extora::storage_error_code::backend_failure),
            boost::beast::http::status::internal_server_error);
}

TEST(HttpProtocolTest, FormatsPortableHttpDates)
{
  const std::optional<std::string> date = formatHttpDate(std::chrono::system_clock::time_point{});
  ASSERT_TRUE(date.has_value());
  EXPECT_EQ(*date, "Thu, 01 Jan 1970 00:00:00 GMT");
}

TEST(HttpProtocolTest, ValidatesHttpTokens)
{
  EXPECT_TRUE(isHttpToken("Owner"));
  EXPECT_TRUE(isHttpToken("project-id"));
  EXPECT_FALSE(isHttpToken(""));
  EXPECT_FALSE(isHttpToken("not a token"));
  EXPECT_FALSE(isHttpToken("line\nbreak"));
}

} // namespace extoraHttpExample
