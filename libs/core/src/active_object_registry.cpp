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

#include "extora/core/active_object_registry.h"

namespace extora::core {

namespace {

constexpr std::size_t initial_active_extent_capacity = 16;

} // namespace

active_object_registry::object_lookup_guard::object_lookup_guard(active_object_registry& registry)
  : m_registry{registry}
  , m_lock{registry.m_registration_mutex}
{}

active_object_registry::object_registration
active_object_registry::object_lookup_guard::register_object(const indexed_object& object)
{
  object_registration registration = m_registry.register_object(object);
  m_lock.unlock();
  return registration;
}

active_object_registry::object_registration::object_registration(active_object_registry& registry,
                                                                 std::vector<extent_key> extents)
  : m_registry{&registry}
  , m_extents{std::move(extents)}
{}

active_object_registry::object_registration::object_registration(object_registration&& other) noexcept
  : m_registry{other.m_registry}
  , m_extents{std::move(other.m_extents)}
{
  other.m_registry = nullptr;
}

active_object_registry::object_registration::~object_registration()
{
  if (m_registry != nullptr) m_registry->unregister_extents(m_extents);
}

active_object_registry::protected_extents_guard::protected_extents_guard(active_object_registry& registry)
  : m_registration_lock{registry.m_registration_mutex}
  , m_extents_lock{registry.m_mutex}
{
  m_extents.reserve(registry.m_active_extents.size());
  for (const auto& active_extent : registry.m_active_extents) {
    physical_extent extent;
    extent.segment_id = active_extent.first.first;
    extent.offset = active_extent.first.second;
    m_extents.push_back(extent);
  }
}

const std::vector<physical_extent>& active_object_registry::protected_extents_guard::extents() const
{
  return m_extents;
}

active_object_registry::active_object_registry()
{
  m_active_extents.reserve(initial_active_extent_capacity);
}

active_object_registry::object_lookup_guard active_object_registry::acquire_object_lookup_guard()
{
  return object_lookup_guard{*this};
}

active_object_registry::object_registration active_object_registry::register_object(const indexed_object& object)
{
  std::vector<extent_key> extents;
  extents.reserve(object.payload.extents.size());

  const std::lock_guard<std::mutex> lock{m_mutex};
  for (const physical_extent& extent : object.payload.extents) {
    const extent_key key{extent.segment_id, extent.offset};
    extents.push_back(key);
    ++m_active_extents[key];
  }

  return object_registration{*this, std::move(extents)};
}

active_object_registry::protected_extents_guard active_object_registry::acquire_protected_extents_guard()
{
  return protected_extents_guard{*this};
}

std::size_t active_object_registry::extent_key_hash::operator()(const extent_key& key) const noexcept
{
  const std::size_t segment_hash = std::hash<std::uint64_t>{}(key.first);
  const std::size_t offset_hash = std::hash<std::uint64_t>{}(key.second);
  return segment_hash ^ (offset_hash + 0x9e3779b9U + (segment_hash << 6U) + (segment_hash >> 2U));
}

void active_object_registry::unregister_extents(const std::vector<extent_key>& extents)
{
  const std::lock_guard<std::mutex> lock{m_mutex};
  for (const extent_key& key : extents) {
    const auto iterator = m_active_extents.find(key);
    if (iterator == m_active_extents.end()) continue;
    if (--iterator->second == 0) m_active_extents.erase(iterator);
  }
}

} // namespace extora::core
