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

#ifndef EXTORA_CORE_OBJECT_STORE_CORE_HELPERS_H
#define EXTORA_CORE_OBJECT_STORE_CORE_HELPERS_H

#include <cstdint>

#include <extora/core/storage_types.h>
#include <extora/storage_error.h>

namespace extora::core::object_store_detail {

storage_error check_read_conditions(const indexed_object& object, const object_conditions& conditions);

storage_error check_write_conditions(const indexed_object& object, const object_conditions& conditions);

object_version_id make_write_version_id(bucket_versioning_status versioning);

object_version_id make_delete_marker_version_id(bucket_versioning_status versioning);

storage_error validate_bucket_name(const bucket_name& bucket);

storage_error validate_object_key(const object_key& key);

storage_error validate_object_address(const bucket_name& bucket, const object_key& key);

std::uint64_t object_content_length(const indexed_object& object);

object_info to_object_info(const indexed_object& object);

stored_object_metadata make_stored_object_metadata(const object_metadata& metadata);

} // namespace extora::core::object_store_detail

#endif // EXTORA_CORE_OBJECT_STORE_CORE_HELPERS_H
