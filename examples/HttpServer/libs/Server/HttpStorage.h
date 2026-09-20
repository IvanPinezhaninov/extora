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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPSTORAGE_H
#define EXTORA_HTTP_EXAMPLE_HTTPSTORAGE_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/spawn.hpp>

#include <extora/asio/async_admission_limiter.h>
#include <extora/asio/async_object_store.h>

namespace extoraHttpExample {

class AsyncStore final : public extora::asio::async_object_store {
public:
  AsyncStore(std::shared_ptr<extora::managed_object_store> store, boost::asio::any_io_executor completionExecutor,
             boost::asio::any_io_executor blockingExecutor, std::size_t maxActiveReads, std::size_t maxQueuedReads,
             std::size_t maxActiveWrites, std::size_t maxQueuedWrites,
             std::chrono::steady_clock::duration queueTimeout);

  [[nodiscard]] extora::asio::admission_permit tryAcquireRead();

  [[nodiscard]] extora::asio::admission_permit tryAcquireWrite();

  template<typename CompletionToken>
  auto asyncAcquireRead(CompletionToken&& token)
  {
    return m_readLimiter.async_acquire(std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto asyncAcquireWrite(CompletionToken&& token)
  {
    return m_writeLimiter.async_acquire(std::forward<CompletionToken>(token));
  }

private:
  extora::asio::async_admission_limiter m_readLimiter;
  extora::asio::async_admission_limiter m_writeLimiter;
};

extora::asio::admission_result acquireRead(AsyncStore& store, boost::asio::yield_context yield);

extora::asio::admission_result acquireWrite(AsyncStore& store, boost::asio::yield_context yield);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPSTORAGE_H
