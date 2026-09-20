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

#include "HttpConditions.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>

#include "HttpRequestCommon.h"

namespace extoraHttpExample {

namespace {

namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

using requestDetail::entityTagListMatches;
using requestDetail::fieldValue;
using requestDetail::parseConditionEtag;
using requestDetail::parseHttpDate;
using requestDetail::startsWithIgnoreCase;
using requestDetail::toString;

bool parseObjectMetadata(const http::fields& fields, extora::object_metadata& metadata, std::string& errorMessage)
{
  metadata = {};
  metadata.content_type = fieldValue(fields, http::field::content_type);
  metadata.cache_control = fieldValue(fields, http::field::cache_control);
  metadata.content_disposition = fieldValue(fields, http::field::content_disposition);
  metadata.content_encoding = fieldValue(fields, http::field::content_encoding);
  metadata.content_language = fieldValue(fields, http::field::content_language);

  const std::optional<std::string> expires = fieldValue(fields, http::field::expires);
  if (expires.has_value()) {
    const std::optional<std::chrono::system_clock::time_point> parsed = parseHttpDate(*expires);
    if (!parsed.has_value()) {
      errorMessage = "Expires must use an IMF-fixdate value";
      return false;
    }
    metadata.expires_at = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::seconds>(parsed->time_since_epoch())};
  }

  constexpr std::string_view customPrefix = "X-Extora-Meta-";
  for (const auto& field : fields) {
    const beast::string_view name = field.name_string();
    if (!startsWithIgnoreCase(name, customPrefix)) continue;
    if (name.size() == customPrefix.size()) {
      errorMessage = "X-Extora-Meta- requires a metadata name";
      return false;
    }

    const std::string metadataName{name.data() + customPrefix.size(), name.size() - customPrefix.size()};
    const auto duplicate =
        std::find_if(metadata.custom_metadata.begin(), metadata.custom_metadata.end(),
                     [&](const extora::metadata_entry& entry) { return beast::iequals(entry.name, metadataName); });
    if (duplicate != metadata.custom_metadata.end()) {
      errorMessage = "duplicate custom metadata header: " + metadataName;
      return false;
    }
    metadata.custom_metadata.push_back(extora::metadata_entry{metadataName, toString(field.value())});
  }
  return true;
}

bool parseObjectConditions(const http::fields& fields, std::string_view headerPrefix,
                           extora::object_conditions& conditions, std::string& errorMessage)
{
  conditions = {};
  const std::string ifMatchName = std::string{headerPrefix} + "If-Match";
  const std::string ifNoneMatchName = std::string{headerPrefix} + "If-None-Match";
  const std::string ifModifiedSinceName = std::string{headerPrefix} + "If-Modified-Since";
  const std::string ifUnmodifiedSinceName = std::string{headerPrefix} + "If-Unmodified-Since";

  const std::optional<std::string> ifMatch = fieldValue(fields, ifMatchName);
  if (ifMatch.has_value()) {
    std::string etag;
    if (!parseConditionEtag(*ifMatch, false, etag)) {
      errorMessage = ifMatchName + " must contain one strong quoted ETag";
      return false;
    }
    conditions.if_match_etag = std::move(etag);
  }

  const std::optional<std::string> ifNoneMatch = fieldValue(fields, ifNoneMatchName);
  if (ifNoneMatch.has_value()) {
    std::string etag;
    if (!parseConditionEtag(*ifNoneMatch, true, etag)) {
      errorMessage = ifNoneMatchName + " must contain one quoted ETag or *";
      return false;
    }
    conditions.if_none_match_etag = std::move(etag);
  }

  const std::optional<std::string> ifModifiedSince = fieldValue(fields, ifModifiedSinceName);
  if (ifModifiedSince.has_value()) {
    conditions.if_modified_since = parseHttpDate(*ifModifiedSince);
    if (!conditions.if_modified_since.has_value()) {
      errorMessage = ifModifiedSinceName + " must use an IMF-fixdate value";
      return false;
    }
  }

  const std::optional<std::string> ifUnmodifiedSince = fieldValue(fields, ifUnmodifiedSinceName);
  if (ifUnmodifiedSince.has_value()) {
    conditions.if_unmodified_since = parseHttpDate(*ifUnmodifiedSince);
    if (!conditions.if_unmodified_since.has_value()) {
      errorMessage = ifUnmodifiedSinceName + " must use an IMF-fixdate value";
      return false;
    }
  }
  return true;
}

bool hasObjectConditions(const http::fields& fields)
{
  return fields.find(http::field::if_match) != fields.end() ||
         fields.find(http::field::if_none_match) != fields.end() ||
         fields.find(http::field::if_modified_since) != fields.end() ||
         fields.find(http::field::if_unmodified_since) != fields.end();
}

extora::storage_error checkObjectConditions(const http::fields& fields, const extora::object_info& object)
{
  const std::optional<std::string> ifMatch = fieldValue(fields, http::field::if_match);
  if (ifMatch.has_value()) {
    if (!entityTagListMatches(*ifMatch, object.etag, true))
      return extora::make_error(extora::storage_error_code::precondition_failed, "If-Match condition failed");
  } else {
    const std::optional<std::string> ifUnmodifiedSince = fieldValue(fields, http::field::if_unmodified_since);
    if (ifUnmodifiedSince.has_value()) {
      const std::optional<std::chrono::system_clock::time_point> condition = parseHttpDate(*ifUnmodifiedSince);
      if (condition.has_value() && object.modified_at > *condition) {
        return extora::make_error(extora::storage_error_code::precondition_failed,
                                  "If-Unmodified-Since condition failed");
      }
    }
  }

  const std::optional<std::string> ifNoneMatch = fieldValue(fields, http::field::if_none_match);
  if (ifNoneMatch.has_value()) {
    if (entityTagListMatches(*ifNoneMatch, object.etag, false))
      return extora::make_error(extora::storage_error_code::not_modified, "If-None-Match condition failed");
  } else {
    const std::optional<std::string> ifModifiedSince = fieldValue(fields, http::field::if_modified_since);
    if (ifModifiedSince.has_value()) {
      const std::optional<std::chrono::system_clock::time_point> condition = parseHttpDate(*ifModifiedSince);
      if (condition.has_value() && object.modified_at <= *condition)
        return extora::make_error(extora::storage_error_code::not_modified, "If-Modified-Since condition failed");
    }
  }

  return {};
}

} // namespace extoraHttpExample
