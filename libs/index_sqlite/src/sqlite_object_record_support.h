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

#ifndef EXTORA_INDEX_SQLITE_OBJECT_RECORD_SUPPORT_H
#define EXTORA_INDEX_SQLITE_OBJECT_RECORD_SUPPORT_H

#include <cstdint>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>
#include <sqlite_states.h>

struct sqlite3;

namespace extora::core::sqlite_detail::object_record_detail {

storage_error find_current_object_id(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                     std::int64_t& object_id);

storage_error make_current_object_state(sqlite3* database, std::int64_t object_id, object_state state,
                                        reclamation_estimate& reclaimable_delta, bool preserve_payload = false);

storage_error detach_object_payload(sqlite3* database, std::int64_t object_id);

storage_error delete_null_versions(sqlite3* database, const bucket_name& bucket, const object_key& key,
                                   std::uint64_t replacement_payload_id, reclamation_estimate& reclaimable_delta);

storage_error load_custom_metadata(sqlite3* database, std::int64_t object_id, indexed_object& result);

storage_error load_payload_extents(sqlite3* database, std::int64_t payload_id, indexed_object& result);

} // namespace extora::core::sqlite_detail::object_record_detail

#endif // EXTORA_INDEX_SQLITE_OBJECT_RECORD_SUPPORT_H
