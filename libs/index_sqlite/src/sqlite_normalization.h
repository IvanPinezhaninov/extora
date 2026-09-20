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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_NORMALIZATION_H
#define EXTORA_INDEX_SQLITE_SQLITE_NORMALIZATION_H

#include <cstdint>
#include <vector>

#include <sqlite3.h>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

storage_error normalize_sqlite_index(sqlite3* database);

storage_error prepare_reusable_extents(sqlite3* database, const std::vector<physical_extent>& protected_extents,
                                       const reclaim_storage_options& options, storage_reclamation_plan& plan,
                                       reclaim_storage_result& result);

storage_error finish_reusable_extents(sqlite3* database, const storage_reclamation_plan& plan,
                                      std::uint64_t segment_capacity, bool& allocator_reset);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_NORMALIZATION_H
