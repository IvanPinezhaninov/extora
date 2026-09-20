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

#ifndef EXTORA_HTTP_EXAMPLE_JSON_H
#define EXTORA_HTTP_EXAMPLE_JSON_H

#include <string>
#include <string_view>

#include <extora/maintenance_types.h>
#include <extora/multipart_types.h>

namespace extoraHttpExample {

const char* bucketVersioningName(extora::bucket_versioning_status status);

const char* checksumTypeName(extora::object_checksum_type type);

std::string errorJson(std::string_view code, std::string_view message);

std::string bucketsJson(const extora::bucket_list& buckets);

std::string bucketUsageJson(const extora::bucket_usage& usage);

std::string bucketVersioningJson(extora::bucket_versioning_status status);

std::string objectsJson(const extora::object_list& objects);

std::string objectVersionsJson(const extora::object_version_list& versions);

std::string multipartUploadsJson(const extora::multipart_upload_list& uploads);

std::string multipartPartsJson(const extora::multipart_part_list& parts);

std::string objectPartsJson(const extora::object_part_list& parts);

std::string multipartUploadJson(const extora::create_multipart_upload_result& upload);

std::string reclamationEstimateJson(const extora::reclamation_estimate& estimate);

std::string reclaimResultJson(const extora::reclaim_storage_result& result);

std::string compactionResultJson(const extora::compact_storage_result& result);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_JSON_H
