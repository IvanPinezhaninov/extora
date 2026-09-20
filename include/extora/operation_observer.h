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

#ifndef EXTORA_OPERATION_OBSERVER_H
#define EXTORA_OPERATION_OBSERVER_H

#include <cstdint>
#include <optional>
#include <string_view>

#include <extora/export.h>
#include <extora/storage_error.h>

namespace extora {

/** @brief Observed operation type. */
enum class operation_type : std::uint8_t {
  put_object,     ///< Object upload.
  open_object,    ///< Object read.
  upload_part,    ///< Multipart part upload.
  compact_storage ///< Storage compaction.
};

/** @brief Observed operation stage. */
enum class operation_stage : std::uint8_t {
  started,   ///< Operation started.
  progress,  ///< More bytes were processed successfully.
  completed, ///< Operation and publication completed.
  failed     ///< Operation returned an error.
};

/** @brief Error details in a failed operation event. */
struct operation_error {
  /** @brief @ref storage_error_code value. */
  storage_error_code code = storage_error_code::none;

  /** @brief Error message. */
  std::string_view message;
};

/**
 * @brief Operation event.
 *
 * String views are valid only during the callback. Optional fields are set
 * only when they apply.
 */
struct operation_event {
  /** @brief ID shared by all events for one operation. */
  std::uint64_t operation_id = 0;

  /** @brief @ref operation_type value. */
  operation_type type = operation_type::put_object;

  /** @brief Current @ref operation_stage value. */
  operation_stage stage = operation_stage::started;

  /** @brief Source or target bucket; empty for store-wide operations. */
  std::string_view bucket;

  /** @brief Source or target key; empty for store-wide operations. */
  std::string_view key;

  /** @brief Upload ID for @ref object_store::upload_part. */
  std::optional<std::string_view> upload_id;

  /** @brief Part number for @ref object_store::upload_part. */
  std::optional<std::uint32_t> part_number;

  /** @brief First byte for an opened object read. */
  std::optional<std::uint64_t> read_offset;

  /** @brief Bytes processed so far. */
  std::uint64_t bytes_processed = 0;

  /** @brief Total byte count when known. */
  std::optional<std::uint64_t> total_bytes;

  /** @brief @ref operation_error for the failed stage. */
  std::optional<operation_error> error;
};

/**
 * @brief Receives streaming operation events.
 *
 * Callbacks are synchronous and may run concurrently. They must not keep
 * event references or string views.
 * Exceptions are ignored. Slow callbacks slow the operation.
 */
class EXTORA_API operation_observer {
public:
  /** @brief Constructs the observer interface. */
  operation_observer() noexcept;

  /** @brief Destroys the observer. */
  virtual ~operation_observer() noexcept;

  /**
   * @brief Handles an operation event.
   *
   * @param[in] event @ref operation_event to handle.
   */
  virtual void on_operation_event(const operation_event& event) = 0;
};

} // namespace extora

#endif // EXTORA_OPERATION_OBSERVER_H
