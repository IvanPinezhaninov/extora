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

#include "HttpQuery.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "HttpRequestCommon.h"

namespace extoraHttpExample {

using requestDetail::parsePageSize;
using requestDetail::parseQueryParameters;
using requestDetail::parseUnsigned;
using requestDetail::percentDecode;
using requestDetail::QueryParameter;

bool parseBucketListOptions(std::string_view query, extora::list_buckets_options& options, std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "prefix")
      options.prefix = std::move(parameter.value);
    else if (parameter.name == "continuation-token")
      options.continuation_token = std::move(parameter.value);
    else if (parameter.name != "max-buckets") {
      errorMessage = "unsupported bucket listing parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_buckets, errorMessage))
      return false;
  }
  return true;
}

bool parseObjectListOptions(std::string_view query, extora::list_objects_options& options, std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "prefix")
      options.prefix = std::move(parameter.value);
    else if (parameter.name == "delimiter")
      options.delimiter = std::move(parameter.value);
    else if (parameter.name == "continuation-token")
      options.continuation_token = std::move(parameter.value);
    else if (parameter.name == "start-after")
      options.start_after = std::move(parameter.value);
    else if (parameter.name != "max-keys") {
      errorMessage = "unsupported query parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_keys, errorMessage))
      return false;
  }
  return true;
}

bool parseVersionListOptions(std::string_view query, extora::list_object_versions_options& options,
                             std::string& errorMessage)
{
  bool hasKeyMarker = false;
  bool hasVersionIdMarker = false;
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "prefix")
      options.prefix = std::move(parameter.value);
    else if (parameter.name == "delimiter")
      options.delimiter = std::move(parameter.value);
    else if (parameter.name == "key-marker") {
      hasKeyMarker = true;
      options.key_marker = std::move(parameter.value);
    } else if (parameter.name == "version-id-marker") {
      hasVersionIdMarker = true;
      if (parameter.value.empty()) {
        errorMessage = "version-id-marker must not be empty";
        return false;
      }
      options.version_id_marker = extora::object_version_id{std::move(parameter.value)};
    } else if (parameter.name != "max-keys") {
      errorMessage = "unsupported version listing parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_keys, errorMessage))
      return false;
  }

  if (hasVersionIdMarker && !hasKeyMarker) {
    errorMessage = "version-id-marker requires key-marker";
    return false;
  }

  return true;
}

bool parseMultipartUploadListOptions(std::string_view query, extora::list_multipart_uploads_options& options,
                                     std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "prefix")
      options.prefix = std::move(parameter.value);
    else if (parameter.name == "delimiter")
      options.delimiter = std::move(parameter.value);
    else if (parameter.name == "key-marker")
      options.key_marker = std::move(parameter.value);
    else if (parameter.name == "upload-id-marker")
      options.upload_id_marker.value = std::move(parameter.value);
    else if (parameter.name != "max-uploads") {
      errorMessage = "unsupported multipart upload listing parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_uploads, errorMessage))
      return false;
  }
  return true;
}

bool parsePartListOptions(std::string_view query, extora::multipart_upload_id& uploadId,
                          extora::list_parts_options& options, std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  bool hasUploadId = false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "upload-id") {
      uploadId.value = std::move(parameter.value);
      hasUploadId = !uploadId.value.empty();
    } else if (parameter.name == "part-number-marker") {
      std::uint64_t marker = 0;
      if (!parseUnsigned(parameter.value, marker) || marker > (std::numeric_limits<std::uint32_t>::max)()) {
        errorMessage = "part-number-marker must be a non-negative 32-bit integer";
        return false;
      }
      options.part_number_marker = static_cast<std::uint32_t>(marker);
    } else if (parameter.name != "max-parts") {
      errorMessage = "unsupported part listing parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_parts, errorMessage))
      return false;
  }
  if (!hasUploadId) errorMessage = "upload-id is required";
  return hasUploadId;
}

bool parseObjectPartListOptions(std::string_view query, extora::list_object_parts_options& options,
                                std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "version-id") {
      if (parameter.value.empty()) {
        errorMessage = "version-id must not be empty";
        return false;
      }
      options.version_id = extora::object_version_id{std::move(parameter.value)};
    } else if (parameter.name == "part-number-marker") {
      std::uint64_t marker = 0;
      if (!parseUnsigned(parameter.value, marker) || marker > (std::numeric_limits<std::uint32_t>::max)()) {
        errorMessage = "part-number-marker must be a non-negative 32-bit integer";
        return false;
      }
      options.part_number_marker = static_cast<std::uint32_t>(marker);
    } else if (parameter.name != "max-parts") {
      errorMessage = "unsupported completed object part parameter: " + parameter.name;
      return false;
    } else if (!parsePageSize(parameter.value, parameter.name, options.max_parts, errorMessage))
      return false;
  }
  return true;
}

bool parseUploadPartOptions(std::string_view query, extora::multipart_upload_id& uploadId, std::uint32_t& partNumber,
                            std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  bool hasUploadId = false;
  bool hasPartNumber = false;
  for (QueryParameter& parameter : parameters) {
    if (parameter.name == "upload-id") {
      uploadId.value = std::move(parameter.value);
      hasUploadId = !uploadId.value.empty();
    } else if (parameter.name == "part-number") {
      std::uint64_t value = 0;
      if (!parseUnsigned(parameter.value, value) || value > (std::numeric_limits<std::uint32_t>::max)()) {
        errorMessage = "part-number must be a positive 32-bit integer";
        return false;
      }
      partNumber = static_cast<std::uint32_t>(value);
      hasPartNumber = partNumber != 0;
    } else {
      errorMessage = "unsupported upload part parameter: " + parameter.name;
      return false;
    }
  }
  if (!hasUploadId || !hasPartNumber) errorMessage = "upload-id and part-number are required";
  return hasUploadId && hasPartNumber;
}

bool parseUploadId(std::string_view query, extora::multipart_upload_id& uploadId, std::string& errorMessage)
{
  std::vector<QueryParameter> parameters;
  if (!parseQueryParameters(query, parameters, errorMessage)) return false;
  if (parameters.size() != 1 || parameters[0].name != "upload-id" || parameters[0].value.empty()) {
    errorMessage = "upload-id is required";
    return false;
  }
  uploadId.value = std::move(parameters[0].value);
  return true;
}

bool parseObjectVersionId(std::string_view query, std::optional<extora::object_version_id>& versionId,
                          std::string& errorMessage)
{
  versionId.reset();
  if (query.empty()) return true;

  constexpr std::string_view prefix = "version-id=";
  if (query.size() <= prefix.size() || query.compare(0, prefix.size(), prefix) != 0 ||
      query.find('&') != std::string_view::npos) {
    errorMessage = "object queries only support version-id";
    return false;
  }

  std::string value;
  if (!percentDecode(query.substr(prefix.size()), value)) {
    errorMessage = "invalid percent-encoded version-id";
    return false;
  }

  versionId = extora::object_version_id{std::move(value)};
  return true;
}

} // namespace extoraHttpExample
