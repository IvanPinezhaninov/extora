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

#ifndef EXTORA_ASIO_BUFFERED_OBJECT_READER_H
#define EXTORA_ASIO_BUFFERED_OBJECT_READER_H

#include <cstddef>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/system/error_code.hpp>

#include <extora/object_stream.h>

namespace extora::asio {

/**
 * @brief Bounded bridge from an asynchronous producer to an object_reader.
 *
 * The producer submits owned chunks with @ref async_write and sends exactly
 * one terminal message with @ref async_finish. When the configured number of
 * chunks is buffered, subsequent writes complete only after
 * @ref object_reader::read consumes space. That call may block and must
 * therefore run only on a blocking executor. The bridge and its outstanding
 * buffers must outlive all operations.
 */
class buffered_object_reader final : public object_reader {
public:
  using executor_type = boost::asio::any_io_executor;

  buffered_object_reader(executor_type executor, std::size_t max_buffered_chunks);

  template<typename CompletionToken>
  auto async_write(std::vector<std::byte> data, CompletionToken&& token)
  {
    return m_channel.async_send(boost::system::error_code{}, message{std::move(data), false, {}},
                                std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_finish(storage_error error, CompletionToken&& token)
  {
    return m_channel.async_send(boost::system::error_code{}, message{{}, true, std::move(error)},
                                std::forward<CompletionToken>(token));
  }

  /** @brief Cancels outstanding producer and consumer channel operations. */
  void cancel();

  object_read_result read(std::byte* data, std::size_t size) override;

private:
  struct message {
    std::vector<std::byte> data;
    bool terminal = false;
    storage_error error;
  };

  using channel_type = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, message)>;

  channel_type m_channel;
  std::vector<std::byte> m_chunk;
  std::size_t m_chunk_offset = 0;
  object_read_result m_terminal_result;
  bool m_terminal = false;
};

} // namespace extora::asio

#endif // EXTORA_ASIO_BUFFERED_OBJECT_READER_H
