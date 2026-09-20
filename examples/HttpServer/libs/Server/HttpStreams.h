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

#ifndef EXTORA_HTTP_EXAMPLE_STREAMS_H
#define EXTORA_HTTP_EXAMPLE_STREAMS_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/error_code.hpp>

#include <extora/asio/async_object_store.h>
#include <extora/asio/buffered_object_reader.h>
#include <extora/object_stream.h>

namespace extoraHttpExample {

using RequestParser = boost::beast::http::request_parser<boost::beast::http::buffer_body>;

class HttpRequestReader final : public extora::object_reader {
public:
  HttpRequestReader(boost::asio::ip::tcp::socket& socket, boost::beast::flat_buffer& buffer, RequestParser& parser,
                    boost::asio::yield_context yield);

  extora::object_read_result read(std::byte* data, std::size_t size) override;

private:
  boost::asio::ip::tcp::socket& m_socket;
  boost::beast::flat_buffer& m_buffer;
  RequestParser& m_parser;
  boost::asio::yield_context m_yield;
};

void pumpRequestBody(boost::asio::ip::tcp::socket& socket, boost::beast::flat_buffer& buffer, RequestParser& parser,
                     extora::asio::buffered_object_reader& reader, boost::asio::yield_context yield);

template<typename Result, typename InitiateOperation>
extora::asio::operation_result<Result>
runStreamingRequest(boost::asio::ip::tcp::socket& socket, boost::beast::flat_buffer& buffer, RequestParser& parser,
                    boost::asio::any_io_executor executor, InitiateOperation&& initiateOperation,
                    boost::asio::yield_context yield)
{
  using Outcome = extora::asio::operation_result<Result>;
  using CompletionChannel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, Outcome)>;

  constexpr std::size_t maxBufferedChunks = 4;
  extora::asio::buffered_object_reader reader{executor, maxBufferedChunks};
  CompletionChannel completion{std::move(executor), 1};
  std::forward<InitiateOperation>(initiateOperation)(reader, [&reader, &completion](Outcome outcome) {
    reader.cancel();
    const bool accepted = completion.try_send(boost::system::error_code{}, std::move(outcome));
    (void)accepted;
    assert(accepted);
  });

  pumpRequestBody(socket, buffer, parser, reader, yield);

  boost::system::error_code completionError;
  Outcome outcome = completion.async_receive(yield[completionError]);
  if (completionError) {
    outcome.error = extora::make_error(extora::storage_error_code::backend_failure,
                                       "streaming storage completion failed: " + completionError.message());
  }
  return outcome;
}

class HttpResponseWriter final : public extora::object_writer {
public:
  using Response = boost::beast::http::response<boost::beast::http::buffer_body>;
  using Serializer = boost::beast::http::response_serializer<boost::beast::http::buffer_body>;

  HttpResponseWriter(boost::asio::ip::tcp::socket& socket, Response& response, Serializer& serializer,
                     boost::asio::yield_context yield, std::uint64_t contentLength);

  extora::storage_error write(const std::byte* data, std::size_t size) override;

  extora::storage_error finish();

private:
  boost::asio::ip::tcp::socket& m_socket;
  Response& m_response;
  Serializer& m_serializer;
  boost::asio::yield_context m_yield;
  std::uint64_t m_contentLength = 0;
  std::uint64_t m_bytesWritten = 0;
};

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_STREAMS_H
