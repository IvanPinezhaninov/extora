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

#include "extora/asio/async_admission_limiter.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio/cancellation_type.hpp>

namespace extora::asio::detail {

admission_waiter_base::admission_waiter_base(boost::asio::any_io_executor timer_executor,
                                             boost::asio::cancellation_slot cancellation_slot)
  : m_timer{std::move(timer_executor)}
  , m_cancellation_slot{std::move(cancellation_slot)}
{}

admission_waiter_base::~admission_waiter_base() = default;

class async_admission_state final : public std::enable_shared_from_this<async_admission_state> {
public:
  async_admission_state(std::size_t max_active, std::size_t max_queued,
                        std::chrono::steady_clock::duration queue_timeout)
    : m_max_active{max_active}
    , m_max_queued{max_queued}
    , m_queue_timeout{queue_timeout}
  {}

  void initiate(std::unique_ptr<admission_waiter_base> waiter)
  {
    enum class decision : std::uint8_t {
      acquire,
      cancel,
      queue,
      reject,
    };
    decision outcome = decision::reject;
    {
      const std::lock_guard<std::mutex> lock{m_mutex};
      waiter->m_id = ++m_next_id;
      if (m_closed.load()) {
        outcome = decision::cancel;
      } else {
        m_has_waiters.store(true);
        std::size_t active = m_active.load();
        while (active < m_max_active && !m_active.compare_exchange_weak(active, active + 1)) {}
        if (active < m_max_active) {
          outcome = decision::acquire;
        } else if (m_max_active != 0 && m_waiters.size() < m_max_queued) {
          const std::uint64_t id = waiter->m_id;
          const std::weak_ptr<async_admission_state> weak_state = weak_from_this();
          waiter->m_timer.expires_after(m_queue_timeout);
          waiter->m_timer.async_wait([weak_state, id](const boost::system::error_code& error) {
            const std::shared_ptr<async_admission_state> state = weak_state.lock();
            if (state && !error) state->timeout(id);
          });
          if (waiter->m_cancellation_slot.is_connected()) {
            waiter->m_cancellation_slot.assign([weak_state, id](boost::asio::cancellation_type_t type) {
              const std::shared_ptr<async_admission_state> state = weak_state.lock();
              if (state && type != boost::asio::cancellation_type::none) state->cancel(id);
            });
          }
          m_waiters.push_back(std::move(waiter));
          outcome = decision::queue;
        }
        m_has_waiters.store(!m_waiters.empty());
      }
    }

    if (outcome == decision::queue) return;
    if (outcome == decision::acquire) {
      complete(std::move(waiter), admission_result{{}, admission_permit{shared_from_this()}});
      return;
    }
    if (outcome == decision::cancel) {
      complete(std::move(waiter), admission_result{make_error(storage_error_code::operation_cancelled,
                                                              "asynchronous admission limiter is closed"),
                                                   {}});
      return;
    }
    complete(std::move(waiter), admission_result{make_error(storage_error_code::concurrency_limit_exceeded,
                                                            "asynchronous operation queue is full"),
                                                 {}});
  }

  bool try_acquire()
  {
    if (m_closed.load() || m_has_waiters.load()) return false;
    std::size_t active = m_active.load();
    while (active < m_max_active) {
      if (m_active.compare_exchange_weak(active, active + 1)) {
        if (!m_closed.load() && !m_has_waiters.load()) return true;
        release();
        return false;
      }
    }
    return false;
  }

  void release() noexcept
  {
    if (!m_has_waiters.load()) {
      --m_active;
      if (!m_has_waiters.load()) return;
      admit_waiter(true);
      return;
    }
    admit_waiter(false);
  }

  void cancel_all()
  {
    std::vector<std::unique_ptr<admission_waiter_base>> waiters;
    {
      const std::lock_guard<std::mutex> lock{m_mutex};
      waiters.reserve(m_waiters.size());
      while (!m_waiters.empty()) {
        waiters.push_back(std::move(m_waiters.front()));
        m_waiters.pop_front();
      }
      m_has_waiters.store(false);
    }
    for (std::unique_ptr<admission_waiter_base>& waiter : waiters) {
      complete(std::move(waiter), admission_result{make_error(storage_error_code::operation_cancelled,
                                                              "asynchronous admission wait was cancelled"),
                                                   {}});
    }
  }

  void close()
  {
    {
      const std::lock_guard<std::mutex> lock{m_mutex};
      m_closed.store(true);
    }
    cancel_all();
  }

private:
  void admit_waiter(bool active_was_released) noexcept
  {
    std::unique_ptr<admission_waiter_base> waiter;
    {
      const std::lock_guard<std::mutex> lock{m_mutex};
      if (!m_closed.load() && !m_waiters.empty()) {
        waiter = std::move(m_waiters.front());
        m_waiters.pop_front();
        if (active_was_released) ++m_active;
      } else if (!active_was_released)
        --m_active;
      m_has_waiters.store(!m_waiters.empty());
    }
    if (waiter) complete(std::move(waiter), admission_result{{}, admission_permit{shared_from_this()}});
  }
  void timeout(std::uint64_t id)
  {
    std::unique_ptr<admission_waiter_base> waiter = remove(id);
    if (waiter) {
      complete(std::move(waiter), admission_result{make_error(storage_error_code::concurrency_limit_exceeded,
                                                              "asynchronous operation queue wait timed out"),
                                                   {}});
    }
  }

  void cancel(std::uint64_t id)
  {
    std::unique_ptr<admission_waiter_base> waiter = remove(id);
    if (waiter) {
      complete(std::move(waiter), admission_result{make_error(storage_error_code::operation_cancelled,
                                                              "asynchronous admission wait was cancelled"),
                                                   {}});
    }
  }

  std::unique_ptr<admission_waiter_base> remove(std::uint64_t id)
  {
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto position =
        std::find_if(m_waiters.begin(), m_waiters.end(), [id](const auto& waiter) { return waiter->m_id == id; });
    if (position == m_waiters.end()) return {};
    std::unique_ptr<admission_waiter_base> waiter = std::move(*position);
    m_waiters.erase(position);
    m_has_waiters.store(!m_waiters.empty());
    return waiter;
  }

  static void complete(std::unique_ptr<admission_waiter_base> waiter, admission_result result)
  {
    waiter->m_timer.cancel();
    if (waiter->m_cancellation_slot.is_connected()) waiter->m_cancellation_slot.clear();
    waiter->deliver(std::move(result));
  }

  std::size_t m_max_active;
  std::size_t m_max_queued;
  std::chrono::steady_clock::duration m_queue_timeout;
  std::mutex m_mutex;
  std::deque<std::unique_ptr<admission_waiter_base>> m_waiters;
  std::atomic<std::size_t> m_active{0};
  std::atomic<bool> m_has_waiters{false};
  std::uint64_t m_next_id = 0;
  std::atomic<bool> m_closed{false};
};

void initiate_admission(const std::shared_ptr<async_admission_state>& state,
                        std::unique_ptr<admission_waiter_base> waiter)
{
  state->initiate(std::move(waiter));
}

void close_admission(const std::shared_ptr<async_admission_state>& state)
{
  state->close();
}

} // namespace extora::asio::detail

namespace extora::asio {

admission_permit::admission_permit(std::shared_ptr<detail::async_admission_state> state)
  : m_state{std::move(state)}
{}

admission_permit::admission_permit(admission_permit&& other) noexcept
  : m_state{std::move(other.m_state)}
{}

admission_permit& admission_permit::operator=(admission_permit&& other) noexcept
{
  if (this != &other) {
    release();
    m_state = std::move(other.m_state);
  }
  return *this;
}

admission_permit::~admission_permit()
{
  release();
}

bool admission_permit::acquired() const noexcept
{
  return static_cast<bool>(m_state);
}

void admission_permit::release() noexcept
{
  std::shared_ptr<detail::async_admission_state> state = std::move(m_state);
  if (state) state->release();
}

async_admission_limiter::async_admission_limiter(executor_type executor, std::size_t max_active, std::size_t max_queued,
                                                 std::chrono::steady_clock::duration queue_timeout)
  : m_executor{std::move(executor)}
  , m_state{std::make_shared<detail::async_admission_state>(max_active, max_queued, queue_timeout)}
{}

async_admission_limiter::~async_admission_limiter()
{
  detail::close_admission(m_state);
}

async_admission_limiter::executor_type async_admission_limiter::get_executor() const noexcept
{
  return m_executor;
}

admission_permit async_admission_limiter::try_acquire()
{
  if (!m_state->try_acquire()) return {};
  return admission_permit{m_state};
}

void async_admission_limiter::cancel()
{
  m_state->cancel_all();
}

} // namespace extora::asio
