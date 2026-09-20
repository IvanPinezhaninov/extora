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

#ifndef EXTORA_CORE_OPERATION_LIMITER_H
#define EXTORA_CORE_OPERATION_LIMITER_H

#include <atomic>
#include <cstddef>
#include <optional>

namespace extora::core {

class operation_limiter {
public:
  class permit {
  public:
    permit(permit&& other) noexcept;
    ~permit();

    permit(const permit&) = delete;
    permit& operator=(const permit&) = delete;
    permit& operator=(permit&&) = delete;

    bool acquired() const;

  private:
    friend class operation_limiter;

    permit(operation_limiter* limiter, bool acquired);

    operation_limiter* m_limiter = nullptr;
    bool m_acquired = false;
  };

  explicit operation_limiter(std::optional<std::size_t> limit);

  permit try_acquire();

private:
  void release();

  std::optional<std::size_t> m_limit;
  std::atomic<std::size_t> m_active{0};
};

} // namespace extora::core

#endif // EXTORA_CORE_OPERATION_LIMITER_H
