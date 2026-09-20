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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_EXTENT_RECORDS_H
#define EXTORA_INDEX_SQLITE_SQLITE_EXTENT_RECORDS_H

#include <cstdint>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

inline void accumulate_reclamation_estimate(reclamation_estimate& total, const reclamation_estimate& delta)
{
  total.reclaimable_bytes += delta.reclaimable_bytes;
  total.reclaimable_extent_count += delta.reclaimable_extent_count;
}

storage_error find_operation_extent_record(sqlite3* database, std::string_view operation_id,
                                           std::uint64_t operation_ordinal, physical_extent& extent, bool& found);

storage_error reserve_reusable_extent_record(sqlite3* database, std::string_view operation_id,
                                             std::uint64_t operation_ordinal, std::uint64_t requested_length,
                                             bool allow_partial, physical_extent& extent, bool& found);

storage_error reserve_exact_reusable_extent_record(sqlite3* database, std::string_view operation_id,
                                                   std::uint64_t operation_ordinal,
                                                   const physical_extent& requested_extent);

storage_error create_free_extent_record(sqlite3* database, const physical_extent& extent);

storage_error reserve_extent_record(sqlite3* database, std::string_view operation_id, std::uint64_t operation_ordinal,
                                    const physical_extent& extent);

storage_error commit_reserved_extent_record(sqlite3* database, const physical_extent& extent,
                                            std::int64_t& physical_extent_id);

storage_error trim_reserved_extent_record(sqlite3* database, const physical_extent& extent,
                                          std::int64_t& physical_extent_id);

storage_error abandon_extent_records(sqlite3* database, const std::vector<physical_extent>& extents,
                                     reclamation_estimate& reclaimable_delta);

storage_error mark_payload_extents_garbage(sqlite3* database, std::int64_t object_id,
                                           reclamation_estimate& reclaimable_delta);

storage_error read_reclamation_estimate(sqlite3* database, reclamation_estimate& estimate);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_EXTENT_RECORDS_H
