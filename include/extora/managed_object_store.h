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

#ifndef EXTORA_MANAGED_OBJECT_STORE_H
#define EXTORA_MANAGED_OBJECT_STORE_H

#include <extora/export.h>
#include <extora/maintenance_types.h>
#include <extora/object_store.h>

namespace extora {

/**
 * @brief Object store with administrative storage operations.
 *
 * Regular adapters should use the @ref object_store interface.
 */
class EXTORA_API managed_object_store : public object_store {
public:
  /** @brief Constructs the managed store interface. */
  managed_object_store() noexcept;

  /** @brief Destroys the managed store. */
  ~managed_object_store() noexcept override;

  /**
   * @brief Gets the logical storage usage of a bucket.
   *
   * Counts retained object versions independently of deduplication. The
   * operation may require work proportional to the bucket contents.
   *
   * @param[in]  bucket Bucket to inspect.
   * @param[out] usage  Receives a @ref bucket_usage value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error get_bucket_usage(const bucket_name& bucket, bucket_usage& usage) = 0;

  /**
   * @brief Gets the current reclamation estimate.
   *
   * Returns a current estimate. Active reads may delay reclamation of reported
   * extents.
   *
   * @param[out] estimate Receives a @ref reclamation_estimate value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error get_reclamation_estimate(reclamation_estimate& estimate) = 0;

  /**
   * @brief Reclaims unused storage.
   *
   * Makes unused storage available for future writes without compacting
   * storage files. Active reads may delay reclamation. Corrupted objects
   * remain quarantined unless their deletion is enabled.
   *
   * @param[out] result  Receives a @ref reclaim_storage_result value.
   * @param[in]  options @ref reclaim_storage_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error reclaim_storage(reclaim_storage_result& result,
                                        const reclaim_storage_options& options = {}) = 0;

  /**
   * @brief Compacts fragmented object payloads.
   *
   * Moves payload extents into a more compact layout and switches affected
   * payloads atomically. Reads and writes may continue concurrently. Readers
   * opened before a switch keep using the old extents until they finish.
   * Progress is reported through the configured @ref operation_observer.
   * Only one compaction may run for a store at a time. If enabled, empty
   * segment files are deleted when possible.
   *
   * @param[out] result Receives a @ref compact_storage_result value.
   * @param[in] options @ref compact_storage_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error compact_storage(compact_storage_result& result,
                                        const compact_storage_options& options = {}) = 0;
};

} // namespace extora

#endif // EXTORA_MANAGED_OBJECT_STORE_H
