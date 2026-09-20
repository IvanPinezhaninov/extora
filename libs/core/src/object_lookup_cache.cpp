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

#include "extora/core/object_lookup_cache.h"

#include <mutex>
#include <utility>

namespace extora::core {

object_lookup_cache::object_lookup_cache(std::size_t capacity)
  : m_capacity{capacity}
{}

bool object_lookup_cache::find(const bucket_name& bucket, const object_key& key, indexed_object& result)
{
  if (m_capacity == 0) return false;

  const cache_key lookup_key{bucket.value, key.value};
  bool promote = false;
  {
    const std::shared_lock<std::shared_mutex> lock{m_mutex};
    const auto found = m_entries.find(lookup_key);
    if (found == m_entries.end()) return false;

    result = found->second->object;
    promote = found->second != m_lru.begin();
  }

  if (!promote) return true;

  std::unique_lock<std::shared_mutex> lock{m_mutex, std::try_to_lock};
  if (!lock.owns_lock()) return true;

  const auto found = m_entries.find(lookup_key);
  if (found != m_entries.end()) m_lru.splice(m_lru.begin(), m_lru, found->second);
  return true;
}

void object_lookup_cache::insert(const indexed_object& object)
{
  if (m_capacity == 0) return;

  const std::unique_lock<std::shared_mutex> lock{m_mutex};
  cache_key key{object.bucket.value, object.key.value};
  const auto found = m_entries.find(key);
  if (found != m_entries.end()) {
    found->second->object = object;
    m_lru.splice(m_lru.begin(), m_lru, found->second);
    return;
  }

  m_lru.push_front(cache_entry{std::move(key), object});
  m_entries.emplace(m_lru.front().key, m_lru.begin());

  if (m_entries.size() <= m_capacity) return;

  const auto oldest = --m_lru.end();
  m_entries.erase(oldest->key);
  m_lru.pop_back();
}

void object_lookup_cache::erase(const bucket_name& bucket, const object_key& key)
{
  if (m_capacity == 0) return;

  const std::unique_lock<std::shared_mutex> lock{m_mutex};
  const auto found = m_entries.find(cache_key{bucket.value, key.value});
  if (found == m_entries.end()) return;

  m_lru.erase(found->second);
  m_entries.erase(found);
}

void object_lookup_cache::clear()
{
  const std::unique_lock<std::shared_mutex> lock{m_mutex};
  m_entries.clear();
  m_lru.clear();
}

bool object_lookup_cache::cache_key::operator==(const cache_key& other) const
{
  return bucket == other.bucket && object == other.object;
}

std::size_t object_lookup_cache::cache_key_hash::operator()(const cache_key& key) const
{
  const std::size_t bucket_hash = std::hash<std::string>{}(key.bucket);
  const std::size_t object_hash = std::hash<std::string>{}(key.object);
  return bucket_hash ^ (object_hash + static_cast<std::size_t>(0x9e3779b9) + (bucket_hash << 6) + (bucket_hash >> 2));
}

} // namespace extora::core
