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

#include "HttpProtocol.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <ostream> // IWYU pragma: keep

#include <boost/beast/http/status.hpp>

namespace extoraHttpExample {

const char* storageErrorCodeName(extora::storage_error_code code)
{
  using Code = extora::storage_error_code;
  switch (code) {
  case Code::none:
    return "none";
  case Code::bucket_already_exists:
    return "bucket_already_exists";
  case Code::bucket_not_found:
    return "bucket_not_found";
  case Code::bucket_not_empty:
    return "bucket_not_empty";
  case Code::object_not_found:
    return "object_not_found";
  case Code::object_version_not_found:
    return "object_version_not_found";
  case Code::object_is_delete_marker:
    return "object_is_delete_marker";
  case Code::object_corrupted:
    return "object_corrupted";
  case Code::invalid_bucket_name:
    return "invalid_bucket_name";
  case Code::invalid_object_key:
    return "invalid_object_key";
  case Code::invalid_list_options:
    return "invalid_list_options";
  case Code::invalid_continuation_token:
    return "invalid_continuation_token";
  case Code::invalid_range:
    return "invalid_range";
  case Code::checksum_mismatch:
    return "checksum_mismatch";
  case Code::unsupported_checksum_algorithm:
    return "unsupported_checksum_algorithm";
  case Code::unsupported_checksum_type:
    return "unsupported_checksum_type";
  case Code::source_failure:
    return "source_failure";
  case Code::sink_failure:
    return "sink_failure";
  case Code::invalid_configuration:
    return "invalid_configuration";
  case Code::storage_in_use:
    return "storage_in_use";
  case Code::backend_failure:
    return "backend_failure";
  case Code::index_failure:
    return "index_failure";
  case Code::insufficient_space:
    return "insufficient_space";
  case Code::permission_denied:
    return "permission_denied";
  case Code::operation_cancelled:
    return "operation_cancelled";
  case Code::concurrency_limit_exceeded:
    return "concurrency_limit_exceeded";
  case Code::precondition_failed:
    return "precondition_failed";
  case Code::not_modified:
    return "not_modified";
  case Code::multipart_upload_not_found:
    return "multipart_upload_not_found";
  case Code::invalid_part:
    return "invalid_part";
  case Code::invalid_part_order:
    return "invalid_part_order";
  case Code::part_too_small:
    return "part_too_small";
  }
  return "unknown";
}

boost::beast::http::status statusForStorageError(extora::storage_error_code code)
{
  using Code = extora::storage_error_code;
  namespace http = boost::beast::http;
  switch (code) {
  case Code::bucket_not_found:
  case Code::object_not_found:
  case Code::object_version_not_found:
  case Code::object_is_delete_marker:
  case Code::multipart_upload_not_found:
    return http::status::not_found;
  case Code::bucket_already_exists:
  case Code::bucket_not_empty:
    return http::status::conflict;
  case Code::invalid_range:
    return http::status::range_not_satisfiable;
  case Code::permission_denied:
    return http::status::forbidden;
  case Code::precondition_failed:
    return http::status::precondition_failed;
  case Code::not_modified:
    return http::status::not_modified;
  case Code::concurrency_limit_exceeded:
    return http::status::service_unavailable;
  case Code::invalid_bucket_name:
  case Code::invalid_object_key:
  case Code::invalid_list_options:
  case Code::invalid_continuation_token:
  case Code::checksum_mismatch:
  case Code::unsupported_checksum_algorithm:
  case Code::unsupported_checksum_type:
  case Code::source_failure:
  case Code::invalid_part:
  case Code::invalid_part_order:
  case Code::part_too_small:
    return http::status::bad_request;
  case Code::operation_cancelled:
    return http::status::request_timeout;
  case Code::none:
  case Code::sink_failure:
  case Code::invalid_configuration:
  case Code::storage_in_use:
  case Code::object_corrupted:
  case Code::backend_failure:
  case Code::index_failure:
  case Code::insufficient_space:
    return http::status::internal_server_error;
  }
  return http::status::internal_server_error;
}

std::optional<std::string> formatHttpDate(std::chrono::system_clock::time_point value)
{
  constexpr std::array<const char*, 7> weekdays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  constexpr std::array<const char*, 12> months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  const std::time_t time = std::chrono::system_clock::to_time_t(value);
  std::tm utcTime{};
#if defined(_WIN32)
  if (::gmtime_s(&utcTime, &time) != 0) return std::nullopt;
#else
  if (::gmtime_r(&time, &utcTime) == nullptr) return std::nullopt;
#endif // defined(_WIN32)

  std::array<char, 30> formatted{};
  const int length = std::snprintf(formatted.data(), formatted.size(), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                                   weekdays[static_cast<std::size_t>(utcTime.tm_wday)], utcTime.tm_mday,
                                   months[static_cast<std::size_t>(utcTime.tm_mon)], utcTime.tm_year + 1900,
                                   utcTime.tm_hour, utcTime.tm_min, utcTime.tm_sec);
  if (length <= 0 || static_cast<std::size_t>(length) >= formatted.size()) return std::nullopt;
  return std::string{formatted.data(), static_cast<std::size_t>(length)};
}

bool isHttpToken(std::string_view value)
{
  constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
  const auto isTokenCharacter = [punctuation](char character) {
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9'))
      return true;
    return punctuation.find(character) != std::string_view::npos;
  };
  return !value.empty() && std::all_of(value.begin(), value.end(), isTokenCharacter);
}

} // namespace extoraHttpExample
