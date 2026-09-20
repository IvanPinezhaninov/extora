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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_COMPACTION_RECORDS_H
#define EXTORA_INDEX_SQLITE_SQLITE_COMPACTION_RECORDS_H

#include <cstdint>
#include <vector>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

struct sqlite3;

namespace extora::core::sqlite_detail {

storage_error load_compaction_layout(sqlite3* database, storage_compaction_layout& layout);

storage_error replace_payload_extent_records(sqlite3* database, std::uint64_t payload_id,
                                             const std::vector<physical_extent>& expected_extents,
                                             const std::vector<physical_extent>& replacement_extents, bool& replaced,
                                             reclamation_estimate& reclaimable_delta);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_COMPACTION_RECORDS_H
