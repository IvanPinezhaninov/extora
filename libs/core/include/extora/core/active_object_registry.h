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

#ifndef EXTORA_CORE_ACTIVE_OBJECT_REGISTRY_H
#define EXTORA_CORE_ACTIVE_OBJECT_REGISTRY_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <extora/core/storage_types.h>

namespace extora::core {

class active_object_registry {
public:
  class object_registration;

  /**
   * @brief Protects object lookup until its extents are registered
   *
   * register_object() pins the payload extents and releases this guard.
   */
  class object_lookup_guard {
  public:
    object_lookup_guard(object_lookup_guard&&) noexcept = default;

    object_lookup_guard(const object_lookup_guard&) = delete;
    object_lookup_guard& operator=(const object_lookup_guard&) = delete;
    object_lookup_guard& operator=(object_lookup_guard&&) = delete;

    object_registration register_object(const indexed_object& object);

  private:
    friend class active_object_registry;

    explicit object_lookup_guard(active_object_registry& registry);

    active_object_registry& m_registry;
    std::shared_lock<std::shared_mutex> m_lock;
  };

  /**
   * @brief Pins a registered object's extents until destruction
   */
  class object_registration {
  public:
    object_registration(object_registration&& other) noexcept;
    ~object_registration();

    object_registration(const object_registration&) = delete;
    object_registration& operator=(const object_registration&) = delete;
    object_registration& operator=(object_registration&&) = delete;

  private:
    friend class active_object_registry;

    using extent_key = std::pair<std::uint64_t, std::uint64_t>;

    object_registration(active_object_registry& registry, std::vector<extent_key> extents);

    active_object_registry* m_registry = nullptr;
    std::vector<extent_key> m_extents;
  };

  /**
   * @brief Prevents new object lookups while exposing protected extents
   */
  class protected_extents_guard {
  public:
    protected_extents_guard(protected_extents_guard&&) noexcept = default;

    protected_extents_guard(const protected_extents_guard&) = delete;
    protected_extents_guard& operator=(const protected_extents_guard&) = delete;
    protected_extents_guard& operator=(protected_extents_guard&&) = delete;

    const std::vector<physical_extent>& extents() const;

  private:
    friend class active_object_registry;

    explicit protected_extents_guard(active_object_registry& registry);

    std::unique_lock<std::shared_mutex> m_registration_lock;
    std::unique_lock<std::mutex> m_extents_lock;
    std::vector<physical_extent> m_extents;
  };

  active_object_registry();

  object_lookup_guard acquire_object_lookup_guard();
  protected_extents_guard acquire_protected_extents_guard();

private:
  using extent_key = object_registration::extent_key;

  struct extent_key_hash {
    std::size_t operator()(const extent_key& key) const noexcept;
  };

  object_registration register_object(const indexed_object& object);
  void unregister_extents(const std::vector<extent_key>& extents);

  std::shared_mutex m_registration_mutex;
  std::mutex m_mutex;
  std::unordered_map<extent_key, std::size_t, extent_key_hash> m_active_extents;
};

} // namespace extora::core

#endif // EXTORA_CORE_ACTIVE_OBJECT_REGISTRY_H
