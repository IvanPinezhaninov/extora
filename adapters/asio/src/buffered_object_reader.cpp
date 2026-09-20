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

#include "extora/asio/buffered_object_reader.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#include <boost/system/error_code.hpp>

#include "extora/storage_error.h"

namespace extora::asio {

buffered_object_reader::buffered_object_reader(executor_type executor, std::size_t max_buffered_chunks)
  : m_channel{std::move(executor), (std::max)(std::size_t{1}, max_buffered_chunks)}
{}

void buffered_object_reader::cancel()
{
  m_channel.cancel();
}

object_read_result buffered_object_reader::read(std::byte* data, std::size_t size)
{
  if (m_terminal) return m_terminal_result;
  if (size == 0) return {0, false, make_error(storage_error_code::source_failure, "empty buffered read destination")};

  for (;;) {
    if (m_chunk_offset < m_chunk.size()) {
      const std::size_t bytes_read = (std::min)(size, m_chunk.size() - m_chunk_offset);
      std::memcpy(data, m_chunk.data() + m_chunk_offset, bytes_read);
      m_chunk_offset += bytes_read;
      if (m_chunk_offset == m_chunk.size()) {
        m_chunk.clear();
        m_chunk_offset = 0;
      }
      return {bytes_read, false, {}};
    }

    std::mutex receive_mutex;
    std::condition_variable receive_condition;
    boost::system::error_code receive_error;
    message next;
    bool received = false;
    auto receive = [&](boost::system::error_code error, message value) {
      const std::lock_guard<std::mutex> lock{receive_mutex};
      receive_error = error;
      next = std::move(value);
      received = true;
      receive_condition.notify_one();
    };

    if (!m_channel.try_receive(receive)) {
      m_channel.async_receive(receive);
      std::unique_lock<std::mutex> lock{receive_mutex};
      receive_condition.wait(lock, [&received] { return received; });
    }

    if (receive_error) {
      m_terminal = true;
      m_terminal_result =
          object_read_result{0, false,
                             make_error(storage_error_code::source_failure,
                                        "buffered object reader cancelled: " + receive_error.message())};
      return m_terminal_result;
    }
    if (next.terminal) {
      m_terminal = true;
      m_terminal_result = {0, succeeded(next.error), std::move(next.error)};
      return m_terminal_result;
    }
    m_chunk = std::move(next.data);
  }
}

} // namespace extora::asio
