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

#ifndef EXTORA_CORE_OPERATION_TRACKER_H
#define EXTORA_CORE_OPERATION_TRACKER_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <extora/operation_observer.h>

namespace extora::core {

class operation_tracker {
public:
  operation_tracker(operation_observer* observer, std::atomic<std::uint64_t>& next_operation_id,
                    std::uint64_t progress_interval, operation_type type, std::string_view bucket, std::string_view key,
                    std::optional<std::uint64_t> total_bytes = std::nullopt,
                    std::optional<std::string_view> upload_id = std::nullopt,
                    std::optional<std::uint32_t> part_number = std::nullopt)
    : m_observer{observer}
    , m_operation_id{observer == nullptr ? 0 : next_operation_id.fetch_add(1, std::memory_order_relaxed)}
    , m_progress_interval{progress_interval}
    , m_type{type}
    , m_bucket{bucket}
    , m_key{key}
    , m_upload_id{upload_id.has_value() ? std::optional<std::string>{std::string{*upload_id}} : std::nullopt}
    , m_part_number{part_number}
    , m_total_bytes{total_bytes}
  {
    notify(operation_stage::started, {});
  }

  operation_tracker(operation_tracker&&) noexcept = default;

  operation_tracker(const operation_tracker&) = delete;
  operation_tracker& operator=(const operation_tracker&) = delete;
  operation_tracker& operator=(operation_tracker&&) = delete;

  void set_total_bytes(std::uint64_t total_bytes)
  {
    m_total_bytes = total_bytes;
  }

  void set_read_offset(std::uint64_t read_offset)
  {
    m_read_offset = read_offset;
  }

  void advance(std::uint64_t bytes_processed)
  {
    m_bytes_processed += bytes_processed;
    if (m_observer == nullptr) return;

    const bool reached_total = m_total_bytes.has_value() && m_bytes_processed == *m_total_bytes;
    const bool reached_interval =
        m_progress_interval == 0 || m_bytes_processed - m_last_progress_bytes >= m_progress_interval;
    if (!reached_total && !reached_interval) return;

    m_last_progress_bytes = m_bytes_processed;
    notify(operation_stage::progress, {});
  }

  void finish(const storage_error& error)
  {
    if (succeeded(error) && !m_total_bytes.has_value()) m_total_bytes = m_bytes_processed;
    notify(failed(error) ? operation_stage::failed : operation_stage::completed, error);
  }

private:
  void notify(operation_stage stage, const storage_error& error) const
  {
    if (m_observer == nullptr) return;

    operation_event event;
    event.operation_id = m_operation_id;
    event.type = m_type;
    event.stage = stage;
    event.bucket = m_bucket;
    event.key = m_key;
    if (m_upload_id.has_value()) event.upload_id = std::string_view{*m_upload_id};
    event.part_number = m_part_number;
    event.read_offset = m_read_offset;
    event.bytes_processed = m_bytes_processed;
    event.total_bytes = m_total_bytes;
    if (failed(error)) event.error = operation_error{error.code, std::string_view{error.message}};

    try {
      m_observer->on_operation_event(event);
    } catch (...) {}
  }

  operation_observer* m_observer = nullptr;
  std::uint64_t m_operation_id = 0;
  std::uint64_t m_progress_interval = 0;
  operation_type m_type = operation_type::put_object;
  std::string m_bucket;
  std::string m_key;
  std::optional<std::string> m_upload_id;
  std::optional<std::uint32_t> m_part_number;
  std::optional<std::uint64_t> m_read_offset;
  std::uint64_t m_bytes_processed = 0;
  std::uint64_t m_last_progress_bytes = 0;
  std::optional<std::uint64_t> m_total_bytes;
};

} // namespace extora::core

#endif // EXTORA_CORE_OPERATION_TRACKER_H
