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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_OBJECT_RECORDS_H
#define EXTORA_INDEX_SQLITE_SQLITE_OBJECT_RECORDS_H

#include <cstdint>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

storage_error publish_object_record(sqlite3* database, const indexed_object& object,
                                    const object_conditions& conditions, reclamation_estimate& reclaimable_delta,
                                    const multipart_upload_id* completed_upload = nullptr);

storage_error find_dedup_object_record(sqlite3* database, const checksum_algorithm_name& checksum_algorithm,
                                       std::string_view value, std::uint64_t content_length, indexed_object& result);

storage_error find_object_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                 indexed_object& result);

storage_error find_object_version_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                         const object_version_id& version_id, indexed_object& result);

storage_error find_object_metadata_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                          indexed_object& result);

storage_error list_object_part_records(sqlite3* database, const indexed_object& object,
                                       std::uint32_t part_number_marker, std::size_t max_parts,
                                       indexed_object_part_page& result);

storage_error delete_object_record(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                   const delete_object_options& options, const object_version_id& marker_version_id,
                                   delete_object_result& result, reclamation_estimate& reclaimable_delta);

storage_error list_object_records(sqlite3* database, const bucket_name& bucket, const list_objects_options& options,
                                  indexed_object_page& result);

storage_error list_object_version_records(sqlite3* database, const bucket_name& bucket,
                                          const list_object_versions_options& options,
                                          indexed_object_version_page& result);

storage_error list_object_generation_records_for_recovery(sqlite3* database, std::vector<indexed_object>& result);

storage_error mark_payload_corrupted_record(sqlite3* database, std::uint64_t payload_id);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_OBJECT_RECORDS_H
