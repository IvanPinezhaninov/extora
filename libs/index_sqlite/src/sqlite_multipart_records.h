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

#ifndef EXTORA_INDEX_SQLITE_MULTIPART_RECORDS_H
#define EXTORA_INDEX_SQLITE_MULTIPART_RECORDS_H

#include <vector>

#include <sqlite3.h>

#include <extora/core/storage_types.h>
#include <extora/maintenance_types.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

storage_error create_multipart_upload_record(sqlite3* database, const indexed_multipart_upload& upload);

storage_error find_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                           indexed_multipart_upload& result);

storage_error store_multipart_part_record(sqlite3* database, const multipart_upload_id& upload_id,
                                          const bucket_name& bucket, const object_key& key,
                                          const indexed_multipart_part& part, reclamation_estimate& reclaimable_delta);

storage_error abort_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                            const bucket_name& bucket, const object_key& key,
                                            reclamation_estimate& reclaimable_delta);

storage_error list_multipart_upload_records(sqlite3* database, const bucket_name& bucket,
                                            std::vector<indexed_multipart_upload>& result);

// Runs inside the object publication transaction.
storage_error finish_multipart_upload_record(sqlite3* database, const multipart_upload_id& upload_id,
                                             reclamation_estimate& reclaimable_delta);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_MULTIPART_RECORDS_H
