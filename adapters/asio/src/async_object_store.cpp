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

#include "extora/asio/async_object_store.h"

#include <stdexcept>
#include <utility>

namespace extora::asio {

async_object_store::async_object_store(std::shared_ptr<managed_object_store> store, executor_type completion_executor,
                                       executor_type blocking_executor)
  : m_store{std::move(store)}
  , m_completion_executor{std::move(completion_executor)}
  , m_blocking_executor{std::move(blocking_executor)}
{
  if (!m_store) throw std::invalid_argument{"store must not be null"};
}

async_object_store::executor_type async_object_store::get_executor() const noexcept
{
  return m_completion_executor;
}

} // namespace extora::asio
