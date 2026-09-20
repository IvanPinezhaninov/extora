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

#include "HttpSession.h"

#include <limits>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/error_code.hpp>

#include "HttpRouter.h"
#include "HttpStreams.h"

namespace extoraHttpExample {

bool requestBodyConsumed(const RequestParser& parser) noexcept
{
  return parser.is_done() || (!parser.chunked() && parser.content_length().value_or(0) == 0);
}

void serveConnection(boost::asio::ip::tcp::socket& socket, AsyncStore& store, boost::asio::yield_context yield)
{
  namespace http = boost::beast::http;

  boost::beast::flat_buffer buffer;
  boost::system::error_code error;
  for (;;) {
    RequestParser parser;
    parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

    http::async_read_header(socket, buffer, parser, yield[error]);
    if (error == http::error::end_of_stream) break;
    if (error) {
      writeInvalidHttpRequest(socket, "invalid HTTP request: " + error.message(), yield);
      break;
    }

    const bool keepAlive = parser.get().keep_alive();
    dispatchRequest(socket, buffer, parser, store, yield);
    if (!keepAlive || !requestBodyConsumed(parser)) break;
  }

  socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, error);
}

} // namespace extoraHttpExample
