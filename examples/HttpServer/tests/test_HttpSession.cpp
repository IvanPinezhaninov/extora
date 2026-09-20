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

#include <boost/asio/buffer.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/system/error_code.hpp>

#include <gtest/gtest.h>

#include "HttpSession.h"

namespace extoraHttpExample {

namespace {

void parseHeader(std::string_view request, RequestParser& parser)
{
  boost::system::error_code error;
  parser.put(boost::asio::buffer(request.data(), request.size()), error);
  ASSERT_TRUE(!error || error == boost::beast::http::error::need_more);
  ASSERT_TRUE(parser.is_header_done());
}

} // namespace

TEST(HttpSessionTest, TreatsRequestsWithoutDeclaredBodiesAsConsumed)
{
  RequestParser getParser;
  parseHeader("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", getParser);
  EXPECT_TRUE(requestBodyConsumed(getParser));

  RequestParser emptyPutParser;
  parseHeader("PUT /bucket HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n", emptyPutParser);
  EXPECT_TRUE(requestBodyConsumed(emptyPutParser));
}

TEST(HttpSessionTest, RequiresDeclaredRequestBodiesToBeRead)
{
  RequestParser contentLengthParser;
  parseHeader("PUT /object HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n", contentLengthParser);
  EXPECT_FALSE(requestBodyConsumed(contentLengthParser));

  RequestParser chunkedParser;
  parseHeader("PUT /object HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n", chunkedParser);
  EXPECT_FALSE(requestBodyConsumed(chunkedParser));
}

} // namespace extoraHttpExample
