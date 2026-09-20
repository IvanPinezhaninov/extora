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

#include "HttpStreams.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/error_code.hpp>

#include "extora/object_stream.h"
#include "extora/storage_error.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

using Tcp = net::ip::tcp;

HttpRequestReader::HttpRequestReader(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser,
                                     net::yield_context yield)
  : m_socket{socket}
  , m_buffer{buffer}
  , m_parser{parser}
  , m_yield{yield}
{}

extora::object_read_result HttpRequestReader::read(std::byte* data, std::size_t size)
{
  if (m_parser.is_done()) return extora::object_read_result{0, true, {}};
  if (size == 0) {
    return extora::object_read_result{
        0, false, extora::make_error(extora::storage_error_code::source_failure, "empty HTTP read buffer")};
  }

  for (;;) {
    http::buffer_body::value_type& body = m_parser.get().body();
    body.data = data;
    body.size = size;

    boost::system::error_code error;
    http::async_read_some(m_socket, m_buffer, m_parser, m_yield[error]);
    const std::size_t bytesRead = size - body.size;
    if (error == http::error::need_buffer) error.clear();
    if (error) {
      return extora::object_read_result{
          bytesRead, false,
          extora::make_error(extora::storage_error_code::source_failure, "HTTP body read failed: " + error.message())};
    }
    if (bytesRead > 0 || m_parser.is_done()) return extora::object_read_result{bytesRead, m_parser.is_done(), {}};
  }
}

void pumpRequestBody(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser,
                     extora::asio::buffered_object_reader& reader, net::yield_context yield)
{
  constexpr std::size_t chunkSize = 64 * 1024;

  for (;;) {
    if (parser.is_done()) {
      boost::system::error_code finishError;
      reader.async_finish({}, yield[finishError]);
      return;
    }

    std::vector<std::byte> chunk(chunkSize);
    http::buffer_body::value_type& body = parser.get().body();
    body.data = chunk.data();
    body.size = chunk.size();

    boost::system::error_code readError;
    http::async_read_some(socket, buffer, parser, yield[readError]);
    const std::size_t bytesRead = chunk.size() - body.size;
    if (readError == http::error::need_buffer) readError.clear();

    if (bytesRead > 0) {
      chunk.resize(bytesRead);
      boost::system::error_code sendError;
      reader.async_write(std::move(chunk), yield[sendError]);
      if (sendError) return;
    }

    if (readError) {
      boost::system::error_code finishError;
      reader.async_finish(extora::make_error(extora::storage_error_code::source_failure,
                                             "HTTP body read failed: " + readError.message()),
                          yield[finishError]);
      return;
    }
  }
}

HttpResponseWriter::HttpResponseWriter(Tcp::socket& socket, Response& response, Serializer& serializer,
                                       net::yield_context yield, std::uint64_t contentLength)
  : m_socket{socket}
  , m_response{response}
  , m_serializer{serializer}
  , m_yield{yield}
  , m_contentLength{contentLength}
{}

extora::storage_error HttpResponseWriter::write(const std::byte* data, std::size_t size)
{
  if (size == 0) return {};
  if (m_bytesWritten > m_contentLength || size > m_contentLength - m_bytesWritten)
    return extora::make_error(extora::storage_error_code::sink_failure, "Extora returned more bytes than declared");

  m_bytesWritten += size;
  m_response.body().data = const_cast<std::byte*>(data);
  m_response.body().size = size;
  m_response.body().more = m_bytesWritten < m_contentLength;

  boost::system::error_code error;
  http::async_write(m_socket, m_serializer, m_yield[error]);
  if (error == http::error::need_buffer) error.clear();
  if (error)
    return extora::make_error(extora::storage_error_code::sink_failure, "HTTP body write failed: " + error.message());
  return {};
}

extora::storage_error HttpResponseWriter::finish()
{
  if (m_bytesWritten != m_contentLength)
    return extora::make_error(extora::storage_error_code::sink_failure, "Extora returned fewer bytes than declared");
  if (m_serializer.is_done()) return {};

  m_response.body().data = nullptr;
  m_response.body().size = 0;
  m_response.body().more = false;

  boost::system::error_code error;
  http::async_write(m_socket, m_serializer, m_yield[error]);
  if (error) {
    return extora::make_error(extora::storage_error_code::sink_failure,
                              "HTTP response finish failed: " + error.message());
  }
  return {};
}

} // namespace extoraHttpExample
