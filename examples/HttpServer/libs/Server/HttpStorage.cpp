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

#include "HttpStorage.h"

#include <chrono>
#include <cstddef>
#include <utility>

#include <boost/asio/any_io_executor.hpp>

namespace extoraHttpExample {

AsyncStore::AsyncStore(std::shared_ptr<extora::managed_object_store> store,
                       boost::asio::any_io_executor completionExecutor, boost::asio::any_io_executor blockingExecutor,
                       std::size_t maxActiveReads, std::size_t maxQueuedReads, std::size_t maxActiveWrites,
                       std::size_t maxQueuedWrites, std::chrono::steady_clock::duration queueTimeout)
  : async_object_store{std::move(store), completionExecutor, std::move(blockingExecutor)}
  , m_readLimiter{completionExecutor, maxActiveReads, maxQueuedReads, queueTimeout}
  , m_writeLimiter{std::move(completionExecutor), maxActiveWrites, maxQueuedWrites, queueTimeout}
{}

extora::asio::admission_permit AsyncStore::tryAcquireRead()
{
  return m_readLimiter.try_acquire();
}

extora::asio::admission_permit AsyncStore::tryAcquireWrite()
{
  return m_writeLimiter.try_acquire();
}

extora::asio::admission_result acquireRead(AsyncStore& store, boost::asio::yield_context yield)
{
  extora::asio::admission_permit permit = store.tryAcquireRead();
  if (permit.acquired()) return extora::asio::admission_result{{}, std::move(permit)};
  return store.asyncAcquireRead(yield);
}

extora::asio::admission_result acquireWrite(AsyncStore& store, boost::asio::yield_context yield)
{
  extora::asio::admission_permit permit = store.tryAcquireWrite();
  if (permit.acquired()) return extora::asio::admission_result{{}, std::move(permit)};
  return store.asyncAcquireWrite(yield);
}

} // namespace extoraHttpExample
