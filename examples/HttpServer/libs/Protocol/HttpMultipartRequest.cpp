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

#include "HttpMultipartRequest.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <boost/json.hpp>

namespace extoraHttpExample {

bool parseCompleteMultipartUploadOptions(std::string_view body, extora::complete_multipart_upload_options& options,
                                         std::string& errorMessage)
{
  options = {};
  boost::system::error_code parseError;
  const boost::json::value value = boost::json::parse(body, parseError);
  if (parseError || !value.is_object()) {
    errorMessage = "completion body must be a JSON object";
    return false;
  }

  const boost::json::object& object = value.as_object();
  const boost::json::value* partsValue = object.if_contains("parts");
  const boost::json::value* checksumValue = object.if_contains("checksum");
  const std::size_t expectedRootSize = checksumValue == nullptr ? 1 : 2;
  if (object.size() != expectedRootSize || partsValue == nullptr || !partsValue->is_array() ||
      (checksumValue != nullptr && !checksumValue->is_string())) {
    errorMessage = "completion body must contain a parts array and an optional checksum string";
    return false;
  }
  if (checksumValue != nullptr) {
    const boost::json::string& checksum = checksumValue->as_string();
    if (checksum.empty()) {
      errorMessage = "checksum must not be empty";
      return false;
    }
    options.expected_checksum = std::string{checksum.data(), checksum.size()};
  }

  const boost::json::array& parts = partsValue->as_array();
  options.parts.reserve(parts.size());
  for (const boost::json::value& partValue : parts) {
    if (!partValue.is_object()) {
      errorMessage = "each completed part must be a JSON object";
      return false;
    }
    const boost::json::object& part = partValue.as_object();
    const boost::json::value* partNumberValue = part.if_contains("part_number");
    const boost::json::value* etagValue = part.if_contains("etag");
    const boost::json::value* partChecksumValue = part.if_contains("checksum");
    const std::size_t expectedPartSize = partChecksumValue == nullptr ? 2 : 3;
    if (part.size() != expectedPartSize || partNumberValue == nullptr || etagValue == nullptr ||
        !etagValue->is_string() || (partChecksumValue != nullptr && !partChecksumValue->is_string())) {
      errorMessage = "each completed part must contain part_number, etag, and an optional checksum string";
      return false;
    }

    std::uint64_t partNumber = 0;
    if (partNumberValue->is_uint64())
      partNumber = partNumberValue->as_uint64();
    else if (partNumberValue->is_int64() && partNumberValue->as_int64() >= 0)
      partNumber = static_cast<std::uint64_t>(partNumberValue->as_int64());
    else {
      errorMessage = "part_number must be a positive 32-bit integer";
      return false;
    }
    if (partNumber == 0 || partNumber > (std::numeric_limits<std::uint32_t>::max)()) {
      errorMessage = "part_number must be a positive 32-bit integer";
      return false;
    }

    const boost::json::string& etag = etagValue->as_string();
    if (etag.empty()) {
      errorMessage = "etag must not be empty";
      return false;
    }
    std::optional<std::string> partChecksum;
    if (partChecksumValue != nullptr) {
      const boost::json::string& checksum = partChecksumValue->as_string();
      if (checksum.empty()) {
        errorMessage = "part checksum must not be empty";
        return false;
      }
      partChecksum = std::string{checksum.data(), checksum.size()};
    }
    options.parts.push_back(extora::completed_multipart_part{static_cast<std::uint32_t>(partNumber),
                                                             std::string{etag.data(), etag.size()}, partChecksum});
  }
  return true;
}

} // namespace extoraHttpExample
