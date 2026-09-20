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

#ifndef EXTORA_ASIO_ASYNC_ADMISSION_LIMITER_H
#define EXTORA_ASIO_ASYNC_ADMISSION_LIMITER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <extora/storage_error.h>

namespace extora::asio {

class async_admission_limiter;

namespace detail {

class async_admission_state;
class admission_waiter_base;

void initiate_admission(const std::shared_ptr<async_admission_state>& state,
                        std::unique_ptr<admission_waiter_base> waiter);
void close_admission(const std::shared_ptr<async_admission_state>& state);

} // namespace detail

/** @brief RAII ownership of one admitted asynchronous operation slot. */
class admission_permit {
public:
  admission_permit() = default;
  admission_permit(admission_permit&& other) noexcept;
  admission_permit& operator=(admission_permit&& other) noexcept;

  /** @brief Releases the owned asynchronous operation slot. */
  ~admission_permit();

  admission_permit(const admission_permit&) = delete;
  admission_permit& operator=(const admission_permit&) = delete;

  [[nodiscard]] bool acquired() const noexcept;

private:
  friend class detail::async_admission_state;
  friend class async_admission_limiter;

  explicit admission_permit(std::shared_ptr<detail::async_admission_state> state);
  void release() noexcept;

  std::shared_ptr<detail::async_admission_state> m_state;
};

/** @brief Result delivered by @ref async_admission_limiter::async_acquire. */
struct [[nodiscard]] admission_result {
  /** @brief @ref storage_error for queueing, timeout, or cancellation. */
  storage_error error;

  /** @brief @ref admission_permit acquired when @ref error indicates success. */
  admission_permit permit;
};

namespace detail {

class admission_waiter_base {
public:
  admission_waiter_base(boost::asio::any_io_executor timer_executor, boost::asio::cancellation_slot cancellation_slot);

  virtual ~admission_waiter_base();

  virtual void deliver(admission_result result) = 0;

  std::uint64_t m_id = 0;
  boost::asio::steady_timer m_timer;
  boost::asio::cancellation_slot m_cancellation_slot;
};

template<typename Handler, typename CompletionExecutor>
class admission_waiter final : public admission_waiter_base {
public:
  admission_waiter(boost::asio::any_io_executor timer_executor, CompletionExecutor completion_executor,
                   boost::asio::cancellation_slot cancellation_slot, Handler handler)
    : admission_waiter_base{std::move(timer_executor), std::move(cancellation_slot)}
    , m_completion_executor{std::move(completion_executor)}
    , m_completion_work{boost::asio::make_work_guard(m_completion_executor)}
    , m_handler{std::move(handler)}
  {}

  void deliver(admission_result result) override
  {
    auto allocator = boost::asio::get_associated_allocator(m_handler);
    boost::asio::post(m_completion_executor, boost::asio::bind_allocator(
                                                 allocator, [handler = std::move(m_handler), result = std::move(result),
                                                             work = std::move(m_completion_work)]() mutable {
                                                   std::move(handler)(std::move(result));
                                                 }));
  }

private:
  CompletionExecutor m_completion_executor;
  boost::asio::executor_work_guard<CompletionExecutor> m_completion_work;
  Handler m_handler;
};

} // namespace detail

/**
 * @brief Bounded asynchronous admission queue.
 *
 * Acquiring never blocks the initiating thread. At most @p max_active permits
 * are live. Further operations wait in FIFO order up to @p max_queued and
 * @p queue_timeout. A full queue or timeout reports
 * @ref storage_error_code::concurrency_limit_exceeded. The limiter may be
 * destroyed before its permits. Member functions and distinct permits may
 * be used concurrently.
 */
class async_admission_limiter {
public:
  using executor_type = boost::asio::any_io_executor;

  async_admission_limiter(executor_type executor, std::size_t max_active, std::size_t max_queued,
                          std::chrono::steady_clock::duration queue_timeout);

  /** @brief Closes the limiter and cancels queued acquisitions. */
  ~async_admission_limiter();

  async_admission_limiter(const async_admission_limiter&) = delete;
  async_admission_limiter& operator=(const async_admission_limiter&) = delete;
  async_admission_limiter(async_admission_limiter&&) = delete;
  async_admission_limiter& operator=(async_admission_limiter&&) = delete;

  [[nodiscard]] executor_type get_executor() const noexcept;

  /** @brief Acquires immediately when capacity is available. */
  [[nodiscard]] admission_permit try_acquire();

  template<typename CompletionToken>
  auto async_acquire(CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(admission_result)>(
        [state = m_state, executor = m_executor](auto&& handler) mutable {
          using Handler = typename std::decay<decltype(handler)>::type;
          auto completion_executor = boost::asio::get_associated_executor(handler, executor);
          using CompletionExecutor = typename std::decay<decltype(completion_executor)>::type;
          boost::asio::cancellation_slot cancellation_slot = boost::asio::get_associated_cancellation_slot(handler);
          auto waiter = std::make_unique<detail::admission_waiter<Handler, CompletionExecutor>>(
              executor, std::move(completion_executor), std::move(cancellation_slot),
              std::forward<decltype(handler)>(handler));
          detail::initiate_admission(state, std::move(waiter));
        },
        token);
  }

  /** @brief Cancels queued acquisitions without revoking live permits. */
  void cancel();

private:
  executor_type m_executor;
  std::shared_ptr<detail::async_admission_state> m_state;
};

} // namespace extora::asio

#endif // EXTORA_ASIO_ASYNC_ADMISSION_LIMITER_H
