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

#include "MultipartHandlers.h"

#include <cstdint>
#include <string>
#include <utility>

#include "HttpConditions.h"
#include "HttpConstants.h"
#include "HttpHandlerSupport.h"
#include "HttpJson.h"
#include "HttpMultipartRequest.h"
#include "HttpQuery.h"
#include "HttpRange.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

void handleListMultipartUploads(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                                net::yield_context yield)
{
  extora::list_multipart_uploads_options options;
  std::string errorMessage;
  if (!parseMultipartUploadListOptions(queryAfterSelector(route.query, uploadsQuery), options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_list_multipart_uploads(route.bucket, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeJsonResponse(socket, version, http::status::ok, multipartUploadsJson(outcome.value), yield);
}

void handleListParts(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                     net::yield_context yield)
{
  extora::multipart_upload_id uploadId;
  extora::list_parts_options options;
  std::string errorMessage;
  if (!parsePartListOptions(route.query, uploadId, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_list_parts(route.bucket, route.key, uploadId, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeJsonResponse(socket, version, http::status::ok, multipartPartsJson(outcome.value), yield);
}

void handleListObjectParts(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store, unsigned version,
                           const Route& route, net::yield_context yield)
{
  extora::list_object_parts_options options;
  std::string errorMessage;
  if (!parseObjectPartListOptions(queryAfterSelector(route.query, objectPartsQuery), options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_list_object_parts(route.bucket, route.key, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  extora::object_part_list& result = outcome.value;
  extora::storage_error error = checkObjectConditions(requestFields, result.object);
  if (extora::failed(error)) {
    if (error.code == extora::storage_error_code::not_modified)
      writeNotModified(socket, version, result.object, yield);
    else
      writeStorageError(socket, version, error, false, yield);
    return;
  }
  writeJsonResponse(socket, version, http::status::ok, objectPartsJson(result), yield);
}

void handleCreateMultipartUpload(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store,
                                 unsigned version, const Route& route, net::yield_context yield)
{
  extora::object_metadata metadata;
  std::string errorMessage;
  if (!parseObjectMetadata(requestFields, metadata, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  extora::create_multipart_upload_options options;
  const auto checksumAlgorithm = requestFields.find(checksumAlgorithmHeader);
  if (checksumAlgorithm != requestFields.end())
    options.checksum_algorithm = extora::checksum_algorithm_name{toString(checksumAlgorithm->value())};
  const auto checksumType = requestFields.find(checksumTypeHeader);
  if (checksumType != requestFields.end()) {
    if (beast::iequals(checksumType->value(), "full_object") || beast::iequals(checksumType->value(), "Full-Object"))
      options.checksum_type = extora::object_checksum_type::full_object;
    else if (beast::iequals(checksumType->value(), "Composite"))
      options.checksum_type = extora::object_checksum_type::composite;
    else {
      writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                         "X-Extora-Checksum-Type must be full_object or composite", false, yield);
      return;
    }
  }

  auto outcome = store.async_create_multipart_upload(route.bucket, route.key, metadata, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeJsonResponse(socket, version, http::status::ok, multipartUploadJson(outcome.value), yield);
}

void handleAbortMultipartUpload(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                                net::yield_context yield)
{
  extora::multipart_upload_id uploadId;
  std::string errorMessage;
  if (!parseUploadId(route.query, uploadId, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }
  auto outcome = store.async_abort_multipart_upload(route.bucket, route.key, uploadId, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeEmptyResponse(socket, version, http::status::no_content, yield);
}

void handleUploadPart(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser, AsyncStore& store,
                      const Route& route, net::yield_context yield)
{
  extora::multipart_upload_id uploadId;
  std::uint32_t partNumber = 0;
  std::string errorMessage;
  if (!parseUploadPartOptions(route.query, uploadId, partNumber, errorMessage)) {
    writeErrorResponse(socket, parser.get().version(), http::status::bad_request, "invalid_request", errorMessage,
                       false, yield);
    return;
  }

  const unsigned version = parser.get().version();
  extora::upload_part_options options;
  const auto contentLength = parser.content_length();
  if (contentLength) options.expected_content_length = *contentLength;
  const auto checksumAlgorithm = parser.get().find(checksumAlgorithmHeader);
  const auto checksum = parser.get().find(checksumHeader);
  if (checksumAlgorithm != parser.get().end()) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                       "multipart checksum algorithm is fixed when the upload is created", false, yield);
    return;
  }
  if (checksum != parser.get().end()) options.expected_checksum = toString(checksum->value());
  auto admission = acquireWrite(store, yield);
  if (extora::failed(admission.error)) {
    writeStorageError(socket, version, admission.error, false, yield);
    return;
  }
  if (expectsContinue(parser.get().base()) && !writeContinue(socket, version, yield)) return;
  auto outcome = runStreamingRequest<extora::upload_part_result>(
      socket, buffer, parser, store.get_executor(),
      [&](extora::object_reader& reader, auto completion) {
        store.async_upload_part(route.bucket, route.key, uploadId, partNumber, reader, options, std::move(completion));
      },
      yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  const extora::upload_part_result& result = outcome.value;

  http::response<http::empty_body> response{http::status::ok, version};
  setCommonHeaders(response);
  response.set(http::field::etag, '"' + result.etag + '"');
  response.set(checksumAlgorithmHeader, result.checksum.checksum_algorithm.value);
  response.set(checksumHeader, result.checksum.value);
  response.set(checksumTypeHeader, checksumTypeName(result.checksum.type));
  response.content_length(0);
  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

void handleUploadPartCopy(Tcp::socket& socket, const http::fields& requestFields, AsyncStore& store, unsigned version,
                          const Route& targetRoute, net::yield_context yield)
{
  extora::multipart_upload_id uploadId;
  std::uint32_t partNumber = 0;
  std::string errorMessage;
  if (!parseUploadPartOptions(targetRoute.query, uploadId, partNumber, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  Route sourceRoute;
  extora::open_object_options openOptions;
  if (!parseCopySource(requestFields, sourceRoute, openOptions.version_id, errorMessage) ||
      !parseObjectConditions(requestFields, copySourceConditionPrefix, openOptions.conditions, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  const auto sourceRange = requestFields.find(copySourceRangeHeader);
  if (sourceRange != requestFields.end()) {
    extora::byte_range range;
    const extora::storage_error rangeError = parseByteRange(toString(sourceRange->value()), range);
    if (extora::failed(rangeError)) {
      writeStorageError(socket, version, rangeError, false, yield);
      return;
    }
    openOptions.range = range;
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

  auto openOutcome = store.async_open_object(sourceRoute.bucket, sourceRoute.key, openOptions, yield);
  extora::storage_error error = openOutcome.error;
  if (error.code == extora::storage_error_code::not_modified)
    error.code = extora::storage_error_code::precondition_failed;
  if (extora::failed(error)) {
    writeStorageError(socket, version, error, false, yield);
    return;
  }
  extora::open_object_result openResult = std::move(openOutcome.value);
  if (!openResult.reader) {
    writeErrorResponse(socket, version, http::status::internal_server_error, "backend_failure",
                       "Extora opened an object without a reader", false, yield);
    return;
  }

  extora::upload_part_options uploadOptions;
  uploadOptions.expected_content_length =
      openResult.byte_range.has_value() ? openResult.byte_range->length : openResult.object.content_length;
  auto uploadOutcome = store.async_upload_part(targetRoute.bucket, targetRoute.key, uploadId, partNumber,
                                               *openResult.reader, uploadOptions, yield);
  if (extora::failed(uploadOutcome.error)) {
    writeStorageError(socket, version, uploadOutcome.error, false, yield);
    return;
  }
  const extora::upload_part_result& result = uploadOutcome.value;

  http::response<http::empty_body> response{http::status::ok, version};
  setCommonHeaders(response);
  response.set(http::field::etag, '"' + result.etag + '"');
  response.set(copySourceVersionIdHeader, openResult.object.version_id.value);
  response.set(checksumAlgorithmHeader, result.checksum.checksum_algorithm.value);
  response.set(checksumHeader, result.checksum.value);
  response.set(checksumTypeHeader, checksumTypeName(result.checksum.type));
  response.content_length(0);
  boost::system::error_code writeError;
  http::async_write(socket, response, yield[writeError]);
}

void handleCompleteMultipartUpload(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser,
                                   AsyncStore& store, const Route& route, net::yield_context yield)
{
  const unsigned version = parser.get().version();
  extora::multipart_upload_id uploadId;
  std::string errorMessage;
  if (!parseUploadId(route.query, uploadId, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }
  if (expectsContinue(parser.get().base()) && !writeContinue(socket, version, yield)) return;

  std::string body;
  extora::storage_error error = readCompletionBody(socket, buffer, parser, yield, body);
  if (extora::failed(error)) {
    writeStorageError(socket, version, error, false, yield);
    return;
  }

  extora::complete_multipart_upload_options options;
  if (!parseCompleteMultipartUploadOptions(body, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", errorMessage, false, yield);
    return;
  }

  auto outcome = store.async_complete_multipart_upload(route.bucket, route.key, uploadId, options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  const extora::put_object_result& result = outcome.value;

  http::response<http::empty_body> response{http::status::ok, version};
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

} // namespace extoraHttpExample
