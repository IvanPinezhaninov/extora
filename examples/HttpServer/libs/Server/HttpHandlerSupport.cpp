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

#include "HttpHandlerSupport.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/system/error_code.hpp>

#include "HttpConditions.h"
#include "HttpJson.h"
#include "HttpQuery.h"
#include "RootPage.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

bool hasQuerySelector(std::string_view query, std::string_view selector)
{
  return query == selector || (query.size() > selector.size() && query.compare(0, selector.size(), selector) == 0 &&
                               query[selector.size()] == '&');
}

std::string_view queryAfterSelector(std::string_view query, std::string_view selector)
{
  return query.size() == selector.size() ? std::string_view{} : query.substr(selector.size() + 1);
}

std::string toString(beast::string_view value)
{
  return std::string{value.data(), value.size()};
}

bool writeJsonResponse(Tcp::socket& socket, unsigned version, http::status status, std::string body,
                       net::yield_context yield)
{
  http::response<http::string_body> response{status, version};
  setCommonHeaders(response);
  response.set(http::field::content_type, "application/json; charset=utf-8");
  response.body() = std::move(body);
  response.prepare_payload();

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeHtmlResponse(Tcp::socket& socket, unsigned version, http::status status, std::string_view body,
                       net::yield_context yield)
{
  http::response<http::string_body> response{status, version};
  setCommonHeaders(response);
  response.set(http::field::content_type, "text/html; charset=utf-8");
  response.body().assign(body.data(), body.size());
  response.prepare_payload();

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeFaviconResponse(Tcp::socket& socket, unsigned version, bool headOnly, net::yield_context yield)
{
  const std::string_view body = faviconSvg();
  if (headOnly) {
    http::response<http::empty_body> response{http::status::ok, version};
    setCommonHeaders(response);
    response.set(http::field::content_type, "image/svg+xml");
    response.set(http::field::cache_control, "public, max-age=86400");
    response.content_length(body.size());

    boost::system::error_code error;
    http::async_write(socket, response, yield[error]);
    return !error;
  }

  http::response<http::string_body> response{http::status::ok, version};
  setCommonHeaders(response);
  response.set(http::field::content_type, "image/svg+xml");
  response.set(http::field::cache_control, "public, max-age=86400");
  response.body().assign(body.data(), body.size());
  response.prepare_payload();

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeEmptyResponse(Tcp::socket& socket, unsigned version, http::status status, net::yield_context yield)
{
  http::response<http::empty_body> response{status, version};
  setCommonHeaders(response);
  response.content_length(0);

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeErrorResponse(Tcp::socket& socket, unsigned version, http::status status, std::string_view code,
                        std::string_view message, bool headOnly, net::yield_context yield)
{
  if (headOnly) return writeEmptyResponse(socket, version, status, yield);
  return writeJsonResponse(socket, version, status, errorJson(code, message), yield);
}

bool writeMethodNotAllowed(Tcp::socket& socket, unsigned version, bool headOnly, net::yield_context yield)
{
  return writeErrorResponse(socket, version, http::status::method_not_allowed, "method_not_allowed",
                            "method is not allowed for this resource", headOnly, yield);
}

bool writeBucketInfoResponse(Tcp::socket& socket, unsigned version, const extora::bucket_info& info,
                             net::yield_context yield)
{
  http::response<http::empty_body> response{http::status::ok, version};
  setCommonHeaders(response);
  const std::int64_t createdAtMilliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(info.created_at.time_since_epoch()).count();
  response.set(bucketCreatedAtHeader, std::to_string(createdAtMilliseconds));
  response.set(bucketVersioningHeader, bucketVersioningName(info.versioning));
  response.content_length(0);

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeStorageError(Tcp::socket& socket, unsigned version, const extora::storage_error& error, bool headOnly,
                       net::yield_context yield)
{
  if (error.code == extora::storage_error_code::concurrency_limit_exceeded) {
    const http::status status = statusForStorageError(error.code);
    if (headOnly) {
      http::response<http::empty_body> response{status, version};
      setCommonHeaders(response);
      response.set(http::field::retry_after, "1");
      response.content_length(0);
      boost::system::error_code writeError;
      http::async_write(socket, response, yield[writeError]);
      return !writeError;
    }

    http::response<http::string_body> response{status, version};
    setCommonHeaders(response);
    response.set(http::field::content_type, "application/json; charset=utf-8");
    response.set(http::field::retry_after, "1");
    response.body() = errorJson(storageErrorCodeName(error.code), error.message);
    response.prepare_payload();
    boost::system::error_code writeError;
    http::async_write(socket, response, yield[writeError]);
    return !writeError;
  }
  return writeErrorResponse(socket, version, statusForStorageError(error.code), storageErrorCodeName(error.code),
                            error.message, headOnly, yield);
}

bool writeNotModified(Tcp::socket& socket, unsigned version, const extora::object_info& info, net::yield_context yield)
{
  http::response<http::empty_body> response{http::status::not_modified, version};
  setCommonHeaders(response);
  setObjectHeaders(response, info);

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool writeContinue(Tcp::socket& socket, unsigned version, net::yield_context yield)
{
  http::response<http::empty_body> response{http::status::continue_, version};
  response.set(http::field::server, serverName);

  boost::system::error_code error;
  http::async_write(socket, response, yield[error]);
  return !error;
}

bool expectsContinue(const http::fields& fields)
{
  const auto position = fields.find(http::field::expect);
  return position != fields.end() && beast::iequals(position->value(), "100-continue");
}

bool parseCopySource(const http::fields& fields, Route& sourceRoute,
                     std::optional<extora::object_version_id>& sourceVersionId, std::string& errorMessage)
{
  const auto copySource = fields.find(copySourceHeader);
  if (copySource == fields.end()) {
    errorMessage = "X-Extora-Copy-Source is required";
    return false;
  }

  sourceRoute = parseRoute(copySource->value());
  if (sourceRoute.type != RouteType::object) {
    errorMessage = sourceRoute.errorMessage.empty() ? "X-Extora-Copy-Source must identify an object"
                                                    : "invalid X-Extora-Copy-Source: " + sourceRoute.errorMessage;
    return false;
  }
  if (!parseObjectVersionId(sourceRoute.query, sourceVersionId, errorMessage)) {
    errorMessage = "invalid X-Extora-Copy-Source: " + errorMessage;
    return false;
  }
  return true;
}

bool parseCopyOptions(const http::fields& fields, extora::copy_object_options& options, std::string& errorMessage)
{
  if (!parseObjectConditions(fields, copySourceConditionPrefix, options.source_conditions, errorMessage) ||
      !parseObjectConditions(fields, {}, options.target_conditions, errorMessage))
    return false;

  const auto checksumAlgorithm = fields.find(checksumAlgorithmHeader);
  if (checksumAlgorithm != fields.end()) {
    options.target_checksum_algorithm = extora::checksum_algorithm_name{toString(checksumAlgorithm->value())};
    if (options.target_checksum_algorithm.value.empty()) {
      errorMessage = "X-Extora-Checksum-Algorithm must not be empty";
      return false;
    }
  }

  const auto directive = fields.find(metadataDirectiveHeader);
  const auto checksum = fields.find(checksumHeader);
  if (checksum != fields.end()) {
    errorMessage = "X-Extora-Checksum is not valid for object copy";
    return false;
  }
  if (directive == fields.end() || beast::iequals(directive->value(), "Copy")) {
    return true;
  }
  if (!beast::iequals(directive->value(), "Replace")) {
    errorMessage = "X-Extora-Metadata-Directive must be Copy or Replace";
    return false;
  }

  options.replace_metadata = true;
  if (!parseObjectMetadata(fields, options.metadata, errorMessage)) return false;
  return true;
}

extora::storage_error readCompletionBody(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser,
                                         net::yield_context yield, std::string& body)
{
  body.clear();
  const auto contentLength = parser.content_length();
  if (contentLength.has_value() && *contentLength > completionBodyLimit)
    return extora::make_error(extora::storage_error_code::source_failure, "completion body exceeds 4 MiB");
  if (contentLength.has_value()) body.reserve(static_cast<std::size_t>(*contentLength));

  HttpRequestReader reader{socket, buffer, parser, yield};
  std::array<std::byte, 16 * 1024> chunk;
  while (true) {
    const extora::object_read_result result = reader.read(chunk.data(), chunk.size());
    if (result.bytes_read > completionBodyLimit - body.size())
      return extora::make_error(extora::storage_error_code::source_failure, "completion body exceeds 4 MiB");
    body.append(reinterpret_cast<const char*>(chunk.data()), result.bytes_read);
    if (extora::failed(result.error)) return result.error;
    if (result.end_of_stream) return {};
    if (result.bytes_read == 0)
      return extora::make_error(extora::storage_error_code::source_failure, "HTTP body reader made no progress");
  }
}

} // namespace extoraHttpExample
