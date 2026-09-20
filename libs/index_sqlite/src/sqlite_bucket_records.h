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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_BUCKET_RECORDS_H
#define EXTORA_INDEX_SQLITE_SQLITE_BUCKET_RECORDS_H

#include <sqlite3.h>

#include <extora/bucket_types.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

storage_error create_bucket_record(sqlite3* database, const bucket_name& bucket);

storage_error delete_bucket_record(sqlite3* database, const bucket_name& bucket);

storage_error find_bucket_record(sqlite3* database, const bucket_name& bucket, bucket_info& result);

storage_error list_bucket_records(sqlite3* database, const list_buckets_options& options, bucket_list& result);

storage_error read_bucket_usage(sqlite3* database, const bucket_name& bucket, bucket_usage& usage);

storage_error set_bucket_versioning_record(sqlite3* database, const bucket_name& bucket,
                                           bucket_versioning_status status);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_BUCKET_RECORDS_H
