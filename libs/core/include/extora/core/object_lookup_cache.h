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

#ifndef EXTORA_CORE_OBJECT_LOOKUP_CACHE_H
#define EXTORA_CORE_OBJECT_LOOKUP_CACHE_H

#include <cstddef>
#include <list>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <extora/core/storage_types.h>

namespace extora::core {

// Thread-safe cache for object metadata and extent locations.
class object_lookup_cache {
public:
  explicit object_lookup_cache(std::size_t capacity);

  bool find(const bucket_name& bucket, const object_key& key, indexed_object& result);

  void insert(const indexed_object& object);

  void erase(const bucket_name& bucket, const object_key& key);

  void clear();

private:
  struct cache_key {
    std::string bucket;
    std::string object;

    bool operator==(const cache_key& other) const;
  };

  struct cache_key_hash {
    std::size_t operator()(const cache_key& key) const;
  };

  struct cache_entry {
    cache_key key;
    indexed_object object;
  };

  using entry_list = std::list<cache_entry>;
  using entry_map = std::unordered_map<cache_key, entry_list::iterator, cache_key_hash>;

  std::size_t m_capacity = 0;
  entry_list m_lru;
  entry_map m_entries;
  mutable std::shared_mutex m_mutex;
};

} // namespace extora::core

#endif // EXTORA_CORE_OBJECT_LOOKUP_CACHE_H
