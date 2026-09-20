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

#include "extora/core/operation_limiter.h"

namespace extora::core {

operation_limiter::permit::permit(permit&& other) noexcept
  : m_limiter{other.m_limiter}
  , m_acquired{other.m_acquired}
{
  other.m_limiter = nullptr;
  other.m_acquired = false;
}

operation_limiter::permit::~permit()
{
  if (m_limiter != nullptr) m_limiter->release();
}

bool operation_limiter::permit::acquired() const
{
  return m_acquired;
}

operation_limiter::permit::permit(operation_limiter* limiter, bool acquired)
  : m_limiter{limiter}
  , m_acquired{acquired}
{}

operation_limiter::operation_limiter(std::optional<std::size_t> limit)
  : m_limit{limit}
{}

void operation_limiter::release()
{
  m_active.fetch_sub(1, std::memory_order_relaxed);
}

operation_limiter::permit operation_limiter::try_acquire()
{
  if (!m_limit.has_value()) return permit{nullptr, true};

  std::size_t active = m_active.load(std::memory_order_relaxed);
  while (active < *m_limit) {
    if (m_active.compare_exchange_weak(active, active + 1, std::memory_order_relaxed)) return permit{this, true};
  }

  return permit{nullptr, false};
}

} // namespace extora::core
