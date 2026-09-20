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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPQUERY_H
#define EXTORA_HTTP_EXAMPLE_HTTPQUERY_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <extora/bucket_types.h>
#include <extora/multipart_types.h>
#include <extora/object_types.h>

namespace extoraHttpExample {

bool parseBucketListOptions(std::string_view query, extora::list_buckets_options& options, std::string& errorMessage);

bool parseObjectListOptions(std::string_view query, extora::list_objects_options& options, std::string& errorMessage);

bool parseVersionListOptions(std::string_view query, extora::list_object_versions_options& options,
                             std::string& errorMessage);

bool parseMultipartUploadListOptions(std::string_view query, extora::list_multipart_uploads_options& options,
                                     std::string& errorMessage);

bool parsePartListOptions(std::string_view query, extora::multipart_upload_id& uploadId,
                          extora::list_parts_options& options, std::string& errorMessage);

bool parseObjectPartListOptions(std::string_view query, extora::list_object_parts_options& options,
                                std::string& errorMessage);

bool parseUploadPartOptions(std::string_view query, extora::multipart_upload_id& uploadId, std::uint32_t& partNumber,
                            std::string& errorMessage);

bool parseUploadId(std::string_view query, extora::multipart_upload_id& uploadId, std::string& errorMessage);

bool parseObjectVersionId(std::string_view query, std::optional<extora::object_version_id>& versionId,
                          std::string& errorMessage);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPQUERY_H
