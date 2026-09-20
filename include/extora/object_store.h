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

#ifndef EXTORA_OBJECT_STORE_H
#define EXTORA_OBJECT_STORE_H

#include <cstdint>

#include <extora/export.h>
#include <extora/multipart_types.h>
#include <extora/object_stream.h>
#include <extora/object_types.h>
#include <extora/storage_error.h>

namespace extora {

/**
 * @brief Object storage interface.
 *
 * Calls may run concurrently. The caller owns all arguments.
 */
class EXTORA_API object_store {
public:
  /** @brief Constructs the store interface. */
  object_store() noexcept;

  /** @brief Destroys the store interface. */
  virtual ~object_store() noexcept;

  /**
   * @brief Creates a bucket.
   *
   * @param[in] bucket @ref bucket_name to create.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error create_bucket(const bucket_name& bucket) = 0;

  /**
   * @brief Deletes an empty bucket.
   *
   * Current objects, old versions, and delete markers make a bucket non-empty.
   *
   * @param[in] bucket @ref bucket_name to delete.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error delete_bucket(const bucket_name& bucket) = 0;

  /**
   * @brief Reads bucket metadata.
   *
   * @param[in]  bucket @ref bucket_name to inspect.
   * @param[out] info   Receives a @ref bucket_info value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error head_bucket(const bucket_name& bucket, bucket_info& info) = 0;

  /**
   * @brief Lists buckets in lexicographic name order.
   *
   * @param[out] result  Receives a @ref bucket_list value.
   * @param[in]  options @ref list_buckets_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_buckets(bucket_list& result, const list_buckets_options& options = {}) = 0;

  /**
   * @brief Enables or suspends bucket versioning.
   *
   * The initial unversioned state cannot be set again.
   *
   * @param[in] bucket        @ref bucket_name to update.
   * @param[in] configuration New @ref bucket_versioning_configuration value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error set_bucket_versioning(const bucket_name& bucket,
                                              bucket_versioning_configuration configuration) = 0;

  /**
   * @brief Gets the bucket versioning status.
   *
   * @param[in]  bucket @ref bucket_name to inspect.
   * @param[out] status Receives the current @ref bucket_versioning_status value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error get_bucket_versioning(const bucket_name& bucket, bucket_versioning_status& status) = 0;

  /**
   * @brief Stores an object from a reader.
   *
   * The object is published atomically. The caller owns @p reader. This call
   * uses it only on the calling thread. Failed writes are not visible.
   * Concurrent writes publish complete versions; the last one is current.
   * An expected checksum is validated before publication and selects the
   * stored public checksum algorithm. Without one, XXH3-128 is stored.
   *
   * @param[in]  bucket   Target @ref bucket_name value.
   * @param[in]  key      Target @ref object_key value.
   * @param[in]  reader   Source @ref object_reader instance.
   * @param[in]  metadata @ref object_metadata supplied by the caller.
   * @param[out] result   Receives a @ref put_object_result value.
   * @param[in]  options  @ref put_object_options for the write.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error put_object(const bucket_name& bucket, const object_key& key, object_reader& reader,
                                   const object_metadata& metadata, put_object_result& result,
                                   const put_object_options& options = {}) = 0;

  /**
   * @brief Starts a multipart upload.
   *
   * Metadata, checksum algorithm, and checksum type are fixed for the upload.
   * The algorithm is used to calculate every part checksum; optional expected
   * part checksums must use it. The final size comes from the selected parts.
   * The current object is unchanged until completion.
   *
   * @param[in]  bucket   Target @ref bucket_name value.
   * @param[in]  key      Target @ref object_key value.
   * @param[in]  metadata Final @ref object_metadata value.
   * @param[out] result   Receives a @ref create_multipart_upload_result value.
   * @param[in]  options  @ref create_multipart_upload_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error create_multipart_upload(const bucket_name& bucket, const object_key& key,
                                                const object_metadata& metadata, create_multipart_upload_result& result,
                                                const create_multipart_upload_options& options = {}) = 0;

  /**
   * @brief Uploads or replaces a multipart part.
   *
   * Different part numbers may be uploaded concurrently. Uploading the same
   * part number again replaces it. A supplied expected checksum value is
   * interpreted using the algorithm selected when the upload was created. The
   * result describes that committed part, including its calculated checksum
   * and creation time.
   *
   * @param[in]  bucket      Target @ref bucket_name value.
   * @param[in]  key         Target @ref object_key value.
   * @param[in]  upload_id   @ref multipart_upload_id value.
   * @param[in]  part_number Part number.
   * @param[in]  reader      Source @ref object_reader instance.
   * @param[out] result      Receives an @ref upload_part_result value.
   * @param[in]  options     @ref upload_part_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error upload_part(const bucket_name& bucket, const object_key& key,
                                    const multipart_upload_id& upload_id, std::uint32_t part_number,
                                    object_reader& reader, upload_part_result& result,
                                    const upload_part_options& options = {}) = 0;

  /**
   * @brief Completes a multipart upload.
   *
   * Publishes the selected parts atomically in the given order after verifying
   * their stored payload integrity. Each part must use the ETag returned by
   * @ref object_store::upload_part. A supplied final checksum value is
   * validated against the full-object or composite contract selected at
   * creation.
   *
   * @param[in]  bucket    Target @ref bucket_name value.
   * @param[in]  key       Target @ref object_key value.
   * @param[in]  upload_id @ref multipart_upload_id value.
   * @param[in]  options   @ref complete_multipart_upload_options value.
   * @param[out] result    Receives a @ref put_object_result value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error complete_multipart_upload(const bucket_name& bucket, const object_key& key,
                                                  const multipart_upload_id& upload_id,
                                                  const complete_multipart_upload_options& options,
                                                  put_object_result& result) = 0;

  /**
   * @brief Aborts a multipart upload.
   *
   * @param[in] bucket    Target @ref bucket_name value.
   * @param[in] key       Target @ref object_key value.
   * @param[in] upload_id @ref multipart_upload_id value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error abort_multipart_upload(const bucket_name& bucket, const object_key& key,
                                               const multipart_upload_id& upload_id) = 0;

  /**
   * @brief Lists uploaded parts.
   *
   * @param[in]  bucket    Target @ref bucket_name value.
   * @param[in]  key       Target @ref object_key value.
   * @param[in]  upload_id @ref multipart_upload_id value.
   * @param[out] result    Receives a @ref multipart_part_list value.
   * @param[in]  options   @ref list_parts_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_parts(const bucket_name& bucket, const object_key& key,
                                   const multipart_upload_id& upload_id, multipart_part_list& result,
                                   const list_parts_options& options = {}) = 0;

  /**
   * @brief Lists incomplete multipart uploads.
   *
   * @param[in]  bucket  @ref bucket_name to list.
   * @param[out] result  Receives a @ref multipart_upload_list value.
   * @param[in]  options @ref list_multipart_uploads_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_multipart_uploads(const bucket_name& bucket, multipart_upload_list& result,
                                               const list_multipart_uploads_options& options = {}) = 0;

  /**
   * @brief Copies an object.
   *
   * Source and target conditions are checked in the same atomic operation.
   * The result identifies the resolved source version and the committed target
   * version and modification time. A requested target checksum algorithm is
   * calculated from the source bytes.
   *
   * @param[in]  source_bucket Source @ref bucket_name value.
   * @param[in]  source_key    Source @ref object_key value.
   * @param[in]  target_bucket Target @ref bucket_name value.
   * @param[in]  target_key    Target @ref object_key value.
   * @param[out] result        Receives a @ref copy_object_result value.
   * @param[in]  options       @ref copy_object_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error copy_object(const bucket_name& source_bucket, const object_key& source_key,
                                    const bucket_name& target_bucket, const object_key& target_key,
                                    copy_object_result& result, const copy_object_options& options = {}) = 0;

  /**
   * @brief Opens an immutable object snapshot for reading.
   *
   * Conditions and ranges are resolved before the call succeeds, so metadata
   * is available before body reads begin. The reader remains valid after
   * overwrite or deletion, but must not outlive this store. It may be moved to
   * another thread, but its @ref object_reader::read calls must not overlap.
   * Destroying it before end of stream cancels the read operation. Integrity
   * verification uses the stored integrity checksum and is available only for
   * a full-object read.
   *
   * If an object version is resolved before an error, @p result contains its
   * metadata and a null reader. This includes delete markers, failed
   * conditions, and invalid ranges. A missing key or version leaves @p result
   * empty.
   *
   * @param[in]  bucket  Source @ref bucket_name value.
   * @param[in]  key     Source @ref object_key value.
   * @param[out] result  Receives snapshot metadata and its pull reader.
   * @param[in]  options @ref open_object_options value.
   * @return @ref storage_error with the open result.
   */
  virtual storage_error open_object(const bucket_name& bucket, const object_key& key, open_object_result& result,
                                    const open_object_options& options = {}) = 0;

  /**
   * @brief Reads object metadata.
   *
   * If an object version is resolved, @p info is populated before delete-marker
   * and condition errors are returned. A missing key or version leaves it
   * empty.
   *
   * @param[in]  bucket  Source @ref bucket_name value.
   * @param[in]  key     Source @ref object_key value.
   * @param[out] info    Receives an @ref object_info value.
   * @param[in]  options @ref head_object_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error head_object(const bucket_name& bucket, const object_key& key, object_info& info,
                                    const head_object_options& options = {}) = 0;

  /**
   * @brief Lists the logical parts of a completed multipart object.
   *
   * Ordinary puts and copies have no part manifest. If an object version is
   * resolved, @p result contains its metadata before delete-marker or
   * condition errors are returned.
   *
   * @param[in]  bucket  Source @ref bucket_name value.
   * @param[in]  key     Source @ref object_key value.
   * @param[out] result  Receives an @ref object_part_list value.
   * @param[in]  options @ref list_object_parts_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_object_parts(const bucket_name& bucket, const object_key& key, object_part_list& result,
                                          const list_object_parts_options& options = {}) = 0;

  /**
   * @brief Deletes an object or version.
   *
   * Without a version ID, an unversioned object is deleted and a versioned
   * bucket gets a delete marker.
   *
   * @param[in]  bucket  Source @ref bucket_name value.
   * @param[in]  key     Source @ref object_key value.
   * @param[out] result  Receives a @ref delete_object_result value.
   * @param[in]  options @ref delete_object_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error delete_object(const bucket_name& bucket, const object_key& key, delete_object_result& result,
                                      const delete_object_options& options = {}) = 0;

  /**
   * @brief Lists current objects.
   *
   * The result never contains a partly published object.
   *
   * @param[in]  bucket  @ref bucket_name to list.
   * @param[out] result  Receives an @ref object_list value.
   * @param[in]  options @ref list_objects_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_objects(const bucket_name& bucket, object_list& result,
                                     const list_objects_options& options = {}) = 0;

  /**
   * @brief Lists object versions and delete markers.
   *
   * An unversioned object appears as its latest version. Grouped prefixes are
   * part of pagination.
   *
   * @param[in]  bucket  @ref bucket_name to list.
   * @param[out] result  Receives an @ref object_version_list value.
   * @param[in]  options @ref list_object_versions_options value.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error list_object_versions(const bucket_name& bucket, object_version_list& result,
                                             const list_object_versions_options& options = {}) = 0;
};

} // namespace extora

#endif // EXTORA_OBJECT_STORE_H
