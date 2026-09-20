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

#ifndef EXTORA_CORE_SQLITE_OBJECT_INDEX_H
#define EXTORA_CORE_SQLITE_OBJECT_INDEX_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <vector>

#include <extora/object_store_options.h>

#include <extora/core/object_index.h>

struct sqlite3;

namespace extora::core {

class sqlite_object_index final : public object_index {
public:
  sqlite_object_index(std::filesystem::path database_path, std::uint64_t segment_capacity,
                      storage_durability durability = storage_durability::balanced);
  ~sqlite_object_index() override;

  sqlite_object_index(const sqlite_object_index&) = delete;
  sqlite_object_index& operator=(const sqlite_object_index&) = delete;

  storage_error open();

  storage_error checkpoint();

  storage_error create_bucket(const bucket_name& bucket) override;

  storage_error delete_bucket(const bucket_name& bucket) override;

  storage_error find_bucket(const bucket_name& bucket, bucket_info& result) override;

  storage_error list_buckets(const list_buckets_options& options, bucket_list& result) override;

  storage_error get_bucket_usage(const bucket_name& bucket, bucket_usage& usage) override;

  storage_error set_bucket_versioning(const bucket_name& bucket, bucket_versioning_status status) override;

  storage_error reserve_extent(std::string_view operation_id, std::uint64_t operation_ordinal,
                               std::uint64_t requested_length, physical_extent& extent,
                               extent_allocation_mode mode = extent_allocation_mode::reuse) override;

  storage_error reserve_extent_at(std::string_view operation_id, std::uint64_t operation_ordinal,
                                  const physical_extent& requested_extent) override;

  storage_error abandon_extents(const std::vector<physical_extent>& extents) override;

  storage_error publish_object(const indexed_object& object,
                               const object_conditions& conditions = object_conditions{}) override;

  storage_error find_dedup_object(const checksum_algorithm_name& checksum_algorithm, std::string_view value,
                                  std::uint64_t content_length, indexed_object& result) override;

  storage_error create_multipart_upload(const indexed_multipart_upload& upload) override;

  storage_error find_multipart_upload(const multipart_upload_id& upload_id, indexed_multipart_upload& result) override;

  storage_error store_multipart_part(const multipart_upload_id& upload_id, const bucket_name& bucket,
                                     const object_key& key, const indexed_multipart_part& part) override;

  storage_error complete_multipart_upload(const multipart_upload_id& upload_id, const indexed_object& object,
                                          const object_conditions& conditions) override;

  storage_error abort_multipart_upload(const multipart_upload_id& upload_id, const bucket_name& bucket,
                                       const object_key& key) override;

  storage_error list_multipart_uploads(const bucket_name& bucket,
                                       std::vector<indexed_multipart_upload>& result) override;

  storage_error find_object(const bucket_name& bucket, const object_key& key, indexed_object& result) override;

  storage_error find_object_version(const bucket_name& bucket, const object_key& key,
                                    const object_version_id& version_id, indexed_object& result) override;

  storage_error find_object_metadata(const bucket_name& bucket, const object_key& key, indexed_object& result) override;

  storage_error list_object_parts(const indexed_object& object, std::uint32_t part_number_marker, std::size_t max_parts,
                                  indexed_object_part_page& result) override;

  storage_error delete_object(const bucket_name& bucket, const object_key& key, const delete_object_options& options,
                              const object_version_id& marker_version_id, delete_object_result& result) override;

  storage_error list_objects(const bucket_name& bucket, const list_objects_options& options,
                             indexed_object_page& result) override;

  storage_error list_object_versions(const bucket_name& bucket, const list_object_versions_options& options,
                                     indexed_object_version_page& result) override;

  storage_error list_object_generations_for_recovery(std::vector<indexed_object>& result) override;

  storage_error mark_payload_corrupted(std::uint64_t payload_id) override;

  storage_error replace_payload_extents(std::uint64_t payload_id, const std::vector<physical_extent>& expected_extents,
                                        const std::vector<physical_extent>& replacement_extents,
                                        bool& replaced) override;

  storage_error prepare_compaction(storage_compaction_layout& layout) override;

  storage_error get_reclamation_estimate(reclamation_estimate& estimate) override;

  storage_error prepare_reclamation(const std::vector<physical_extent>& protected_extents,
                                    const reclaim_storage_options& options, storage_reclamation_plan& plan,
                                    reclaim_storage_result& result) override;

  storage_error finish_reclamation(const storage_reclamation_plan& plan) override;

private:
  class read_connection final {
  public:
    read_connection(sqlite_object_index& index, storage_error& error);

    read_connection(const read_connection&) = delete;
    read_connection& operator=(const read_connection&) = delete;

    sqlite3* database() const;

  private:
    std::unique_lock<std::mutex> m_lock;
    sqlite3* m_database = nullptr;
  };

  storage_error acquire_read_database(std::unique_lock<std::mutex>& lock, sqlite3*& database);
  storage_error open_read_database(sqlite3*& database);
  storage_error initialize_allocator();
  storage_error load_reclamation_estimate();
  void apply_reclamation_delta(const reclamation_estimate& delta);

  static constexpr std::size_t read_connection_capacity = 8;

  std::filesystem::path m_database_path;
  std::uint64_t m_segment_capacity = 0;
  storage_durability m_durability = storage_durability::balanced;
  sqlite3* m_database = nullptr;
  std::array<sqlite3*, read_connection_capacity> m_read_databases{};
  std::array<std::mutex, read_connection_capacity> m_read_database_mutexes;
  std::atomic<std::size_t> m_next_read_database{0};
  std::uint64_t m_next_segment_id = 1;
  std::uint64_t m_next_offset = 0;
  reclamation_estimate m_reclamation_estimate;
};

} // namespace extora::core

#endif // EXTORA_CORE_SQLITE_OBJECT_INDEX_H
