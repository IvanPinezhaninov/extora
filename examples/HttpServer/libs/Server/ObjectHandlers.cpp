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

#include "ObjectHandlers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/system/error_code.hpp>

#include "HttpConditions.h"
#include "HttpConstants.h"
#include "HttpHandlerSupport.h"
#include "HttpJson.h"
#include "HttpQuery.h"
#include "HttpRange.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

void handleListObjects(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                       net::yield_context yield)
{
  extora::list_objects_options options;
  std::string errorMessage;
  if (!parseObjectListOptions(route.query, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_list_objects(route.bucket, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, objectsJson(outcome.value), yield);
}

void handleListObjectVersions(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                              net::yield_context yield)
{
  const std::string_view parameters = queryAfterSelector(route.query, versionsQuery);
  extora::list_object_versions_options options;
  std::string errorMessage;
  if (!parseVersionListOptions(parameters, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_list_object_versions(route.bucket, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, objectVersionsJson(outcome.value), yield);
}

void handlePutObject(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser, AsyncStore& store,
                     const Route& route, net::yield_context yield)
{
  const unsigned version = parser.get().version();
  extora::object_metadata metadata;
  std::string errorMessage;
  if (!parseObjectMetadata(parser.get().base(), metadata, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }
  extora::put_object_options options;
  const auto contentLength = parser.content_length();
  if (contentLength) options.expected_content_length = *contentLength;

  const auto checksumAlgorithm = parser.get().find(checksumAlgorithmHeader);
  const auto checksum = parser.get().find(checksumHeader);
  if ((checksumAlgorithm == parser.get().end()) != (checksum == parser.get().end())) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                       "object checksum requires both X-Extora-Checksum-Algorithm and X-Extora-Checksum", false, yield);
    return;
  }
  if (checksumAlgorithm != parser.get().end()) {
    const std::string algorithm = toString(checksumAlgorithm->value());
    if (algorithm.empty()) {
      writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                         "X-Extora-Checksum-Algorithm must not be empty", false, yield);
      return;
    }
    options.expected_checksum =
        extora::object_checksum{extora::checksum_algorithm_name{algorithm}, toString(checksum->value())};
  }
  if (!parseObjectConditions(parser.get().base(), {}, options.conditions, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  auto admission = acquireWrite(store, yield);
  if (extora::failed(admission.error)) {
    writeStorageError(socket, version, admission.error, false, yield);
    return;
  }

  if (expectsContinue(parser.get().base()) && !writeContinue(socket, version, yield)) return;

  auto outcome = runStreamingRequest<extora::put_object_result>(
      socket, buffer, parser, store.get_executor(),
      [&](extora::object_reader& reader, auto completion) {
        store.async_put_object(route.bucket, route.key, reader, metadata, options, std::move(completion));
      },
      yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  const extora::put_object_result& result = outcome.value;

  http::response<http::empty_body> response{http::status::created, version};
  setCommonHeaders(response);
  response.set(http::field::etag, '"' + result.etag + '"');
  response.set(versionIdHeader, result.version_id.value);
  response.set(checksumAlgorithmHeader, result.checksum.checksum_algorithm.value);
  response.set(checksumHeader, result.checksum.value);
  response.set(checksumTypeHeader, checksumTypeName(result.checksum.type));
  response.content_length(0);

  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

void handleCopyObject(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store, unsigned version,
                      const Route& targetRoute, net::yield_context yield)
{
  extora::copy_object_options options;
  Route sourceRoute;
  std::string errorMessage;
  if (!parseCopySource(requestFields, sourceRoute, options.source_version_id, errorMessage) ||
      !parseCopyOptions(requestFields, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  auto readAdmission = acquireRead(store, yield);
  if (extora::failed(readAdmission.error)) {
    writeStorageError(socket, version, readAdmission.error, false, yield);
    return;
  }
  auto writeAdmission = acquireWrite(store, yield);
  if (extora::failed(writeAdmission.error)) {
    writeStorageError(socket, version, writeAdmission.error, false, yield);
    return;
  }

  auto outcome =
      store.async_copy_object(sourceRoute.bucket, sourceRoute.key, targetRoute.bucket, targetRoute.key, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  const extora::copy_object_result& result = outcome.value;

  http::response<http::empty_body> response{http::status::ok, version};
  setCommonHeaders(response);
  response.set(http::field::etag, '"' + result.etag + '"');
  response.set(copySourceVersionIdHeader, result.source_version_id.value);
  response.set(versionIdHeader, result.version_id.value);
  response.set(checksumAlgorithmHeader, result.checksum.checksum_algorithm.value);
  response.set(checksumHeader, result.checksum.value);
  response.set(checksumTypeHeader, checksumTypeName(result.checksum.type));
  setDateHeader(response, http::field::last_modified, result.modified_at);
  response.content_length(0);

  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

void handleHeadObject(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store, unsigned version,
                      const Route& route, const std::optional<extora::object_version_id>& versionId,
                      net::yield_context yield)
{
  extora::head_object_options options;
  options.version_id = versionId;
  auto outcome = store.async_head_object(route.bucket, route.key, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, true, yield);
    return;
  }
  extora::object_info info = std::move(outcome.value);

  extora::storage_error error = checkObjectConditions(requestFields, info);
  if (extora::failed(error)) {
    if (error.code == extora::storage_error_code::not_modified)
      writeNotModified(socket, version, info, yield);
    else
      writeStorageError(socket, version, error, true, yield);
    return;
  }

  http::response<http::empty_body> response{http::status::ok, version};
  setCommonHeaders(response);
  setObjectHeaders(response, info);
  response.content_length(info.content_length);

  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

void handleGetObject(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store, unsigned version,
                     const Route& route, const std::optional<extora::object_version_id>& versionId,
                     net::yield_context yield)
{
  extora::open_object_options options;
  options.version_id = versionId;
  std::optional<extora::resolved_byte_range> resolvedRange;
  extora::object_info info;
  extora::storage_error error;
  if (hasObjectConditions(requestFields)) {
    extora::head_object_options headOptions;
    headOptions.version_id = versionId;
    auto headOutcome = store.async_head_object(route.bucket, route.key, headOptions, yield);
    if (extora::failed(headOutcome.error)) {
      writeStorageError(socket, version, headOutcome.error, false, yield);
      return;
    }
    info = std::move(headOutcome.value);

    error = checkObjectConditions(requestFields, info);
    if (extora::failed(error)) {
      if (error.code == extora::storage_error_code::not_modified)
        writeNotModified(socket, version, info, yield);
      else
        writeStorageError(socket, version, error, false, yield);
      return;
    }

    options.version_id = info.version_id;
    options.conditions.if_match_etag = info.etag;
    error = parseRange(requestFields, info.content_length, options.range, resolvedRange);
  } else {
    error = parseRequestedRange(requestFields, options.range);
  }
  if (extora::failed(error)) {
    writeStorageError(socket, version, error, false, yield);
    return;
  }

  auto admission = acquireRead(store, yield);
  if (extora::failed(admission.error)) {
    writeStorageError(socket, version, admission.error, false, yield);
    return;
  }

  auto openOutcome = store.async_open_object(route.bucket, route.key, options, yield);
  if (extora::failed(openOutcome.error)) {
    writeStorageError(socket, version, openOutcome.error, false, yield);
    return;
  }
  extora::open_object_result result = std::move(openOutcome.value);
  info = result.object;
  resolvedRange = result.byte_range;

  const std::uint64_t contentLength = resolvedRange.has_value() ? resolvedRange->length : info.content_length;
  const http::status status = resolvedRange.has_value() ? http::status::partial_content : http::status::ok;

  http::response<http::buffer_body> response{status, version};
  setCommonHeaders(response);
  setObjectHeaders(response, info);
  response.content_length(contentLength);
  if (resolvedRange.has_value()) {
    const std::uint64_t lastOffset = resolvedRange->offset + resolvedRange->length - 1;
    response.set(http::field::content_range, "bytes " + std::to_string(resolvedRange->offset) + '-' +
                                                 std::to_string(lastOffset) + '/' +
                                                 std::to_string(info.content_length));
  }
  response.body().data = nullptr;
  response.body().size = 0;
  response.body().more = contentLength > 0;

  http::response_serializer<http::buffer_body> serializer{response};
  HttpResponseWriter writer{socket, response, serializer, yield, contentLength};
  std::array<std::byte, 64 * 1024> bodyBuffer;
  while (true) {
    auto readOutcome = store.async_read_some(*result.reader, bodyBuffer.data(), bodyBuffer.size(), yield);
    if (extora::failed(readOutcome.error)) {
      error = std::move(readOutcome.error);
      break;
    }
    const extora::object_read_result& readResult = readOutcome.value;
    if (readResult.bytes_read > 0) {
      error = writer.write(bodyBuffer.data(), readResult.bytes_read);
      if (extora::failed(error)) break;
    }
    if (extora::failed(readResult.error)) {
      error = readResult.error;
      break;
    }
    if (readResult.end_of_stream) break;
    if (readResult.bytes_read == 0) {
      error = extora::make_error(extora::storage_error_code::backend_failure, "object reader made no progress");
      break;
    }
  }
  if (extora::failed(error)) {
    std::cerr << "GET " << route.bucket.value << '/' << route.key.value
              << " failed after response headers: " << error.message << std::endl;
    return;
  }

  error = writer.finish();
  if (extora::failed(error))
    std::cerr << "GET " << route.bucket.value << '/' << route.key.value << " failed: " << error.message << std::endl;
}

void handleDeleteObject(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        const std::optional<extora::object_version_id>& versionId, net::yield_context yield)
{
  extora::delete_object_options options;
  options.version_id = versionId;
  auto outcome = store.async_delete_object(route.bucket, route.key, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  const extora::delete_object_result& result = outcome.value;

  http::response<http::empty_body> response{http::status::no_content, version};
  setCommonHeaders(response);
  response.set(versionIdHeader, result.version_id.value);
  response.set(deleteMarkerHeader, result.is_delete_marker ? "true" : "false");
  response.content_length(0);

  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

} // namespace extoraHttpExample
