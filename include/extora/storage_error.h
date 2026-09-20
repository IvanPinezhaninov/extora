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

#ifndef EXTORA_STORAGE_ERROR_H
#define EXTORA_STORAGE_ERROR_H

#include <cstdint>
#include <string>
#include <utility>

namespace extora {

/** @brief Public operation result code. */
enum class storage_error_code : std::uint8_t {
  none,                           ///< Success.
  bucket_already_exists,          ///< Bucket already exists.
  bucket_not_found,               ///< Bucket was not found.
  bucket_not_empty,               ///< Bucket is not empty.
  object_not_found,               ///< Object was not found.
  object_version_not_found,       ///< Requested object version was not found.
  object_is_delete_marker,        ///< Requested object resolves to a delete marker.
  object_corrupted,               ///< Object is quarantined after an integrity failure.
  invalid_bucket_name,            ///< Bucket name is invalid.
  invalid_object_key,             ///< Object key is invalid.
  invalid_list_options,           ///< Listing options are invalid.
  invalid_continuation_token,     ///< Listing continuation token is invalid.
  invalid_range,                  ///< Byte range is invalid.
  checksum_mismatch,              ///< Checksum does not match.
  unsupported_checksum_algorithm, ///< Checksum algorithm is unsupported.
  unsupported_checksum_type,      ///< Checksum calculation type is unsupported.
  source_failure,                 ///< Object reader failed.
  sink_failure,                   ///< Object writer failed.
  invalid_configuration,          ///< Store configuration is invalid.
  storage_in_use,                 ///< Storage root is already in use.
  backend_failure,                ///< Data backend failed.
  index_failure,                  ///< Index backend failed.
  insufficient_space,             ///< Storage has insufficient space.
  permission_denied,              ///< Operation is not permitted.
  operation_cancelled,            ///< Operation was cancelled.
  concurrency_limit_exceeded,     ///< Concurrency limit was reached.
  precondition_failed,            ///< Object condition failed.
  not_modified,                   ///< Object was not modified.
  multipart_upload_not_found,     ///< Multipart upload was not found.
  invalid_part,                   ///< Multipart part is invalid.
  invalid_part_order,             ///< Multipart parts are out of order.
  part_too_small                  ///< Multipart part is too small.
};

/** @brief Error returned by public operations. */
struct [[nodiscard]] storage_error {
  /** @brief @ref storage_error_code value. */
  storage_error_code code = storage_error_code::none;

  /** @brief Error message. */
  std::string message;
};

/**
 * @brief Creates an operation error.
 *
 * @param[in] code    Error code.
 * @param[in] message Error message.
 * @return New @ref storage_error value.
 */
inline storage_error make_error(storage_error_code code, std::string message)
{
  return storage_error{code, std::move(message)};
}

/**
 * @brief Checks for failure.
 *
 * @param[in] error Error to check.
 * @return true on failure.
 */
[[nodiscard]] inline bool failed(const storage_error& error) noexcept
{
  return error.code != storage_error_code::none;
}

/**
 * @brief Checks for success.
 *
 * @param[in] error Error to check.
 * @return true on success.
 */
[[nodiscard]] inline bool succeeded(const storage_error& error) noexcept
{
  return !failed(error);
}

} // namespace extora

#endif // EXTORA_STORAGE_ERROR_H
