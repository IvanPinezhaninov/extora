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

#include "HttpRouter.h"

#include <optional>
#include <string>
#include <string_view>

#include <boost/beast/http.hpp>

#include "BucketHandlers.h"
#include "HttpConstants.h"
#include "HttpHandlerSupport.h"
#include "HttpQuery.h"
#include "HttpRoute.h"
#include "MultipartHandlers.h"
#include "ObjectHandlers.h"
#include "RootPage.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

using Tcp = net::ip::tcp;

void dispatchRequest(Tcp::socket& socket, beast::flat_buffer& buffer, RequestParser& parser, AsyncStore& store,
                     net::yield_context yield)
{
  const http::request<http::buffer_body>& request = parser.get();
  const unsigned version = request.version();
  const bool headOnly = request.method() == http::verb::head;
  const Route route = parseRoute(request.target());
  if (route.type == RouteType::invalid) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", route.errorMessage, headOnly,
                       yield);
    return;
  }

  if (route.type != RouteType::buckets && route.type != RouteType::bucket && route.type != RouteType::object &&
      !route.query.empty()) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                       "query parameters are only supported for bucket and object resources", headOnly, yield);
    return;
  }

  if (route.type == RouteType::root) {
    if (request.method() == http::verb::get)
      writeHtmlResponse(socket, version, http::status::ok, rootPageHtml(), yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::favicon) {
    if (request.method() == http::verb::get || headOnly)
      writeFaviconResponse(socket, version, headOnly, yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::health) {
    if (request.method() == http::verb::get)
      writeJsonResponse(socket, version, http::status::ok, "{\"status\":\"ok\"}\n", yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::buckets) {
    if (request.method() == http::verb::get)
      handleListBuckets(socket, store, version, route, yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::bucket_usage) {
    if (request.method() == http::verb::get)
      handleGetBucketUsage(socket, store, version, route, yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::reclamation) {
    if (request.method() == http::verb::get)
      handleGetReclamationEstimate(socket, store, version, yield);
    else if (request.method() == http::verb::post)
      handleReclaimStorage(socket, store, version, yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::compaction) {
    if (request.method() == http::verb::post)
      handleCompactStorage(socket, store, version, yield);
    else
      writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (route.type == RouteType::bucket) {
    if (hasQuerySelector(route.query, versionsQuery)) {
      if (request.method() == http::verb::get)
        handleListObjectVersions(socket, store, version, route, yield);
      else
        writeMethodNotAllowed(socket, version, headOnly, yield);
      return;
    }
    if (hasQuerySelector(route.query, uploadsQuery)) {
      if (request.method() == http::verb::get)
        handleListMultipartUploads(socket, store, version, route, yield);
      else
        writeMethodNotAllowed(socket, version, headOnly, yield);
      return;
    }
    if (route.query == "versioning") {
      if (request.method() == http::verb::get)
        handleGetBucketVersioning(socket, store, version, route, yield);
      else
        writeMethodNotAllowed(socket, version, headOnly, yield);
      return;
    }
    if (route.query.compare(0, 11, "versioning=") == 0) {
      if (request.method() != http::verb::put) {
        writeMethodNotAllowed(socket, version, headOnly, yield);
        return;
      }

      const std::string_view status{route.query.data() + 11, route.query.size() - 11};
      if (status == "enabled") {
        handlePutBucketVersioning(socket, store, version, route, extora::bucket_versioning_configuration::enabled,
                                  yield);
      } else if (status == "suspended") {
        handlePutBucketVersioning(socket, store, version, route, extora::bucket_versioning_configuration::suspended,
                                  yield);
      } else {
        writeErrorResponse(socket, version, http::status::bad_request, "invalid_request",
                           "versioning must be enabled or suspended", false, yield);
      }
      return;
    }

    switch (request.method()) {
    case http::verb::put:
      if (!route.query.empty()) break;
      handleCreateBucket(socket, store, version, route, yield);
      return;
    case http::verb::get:
      handleListObjects(socket, store, version, route, yield);
      return;
    case http::verb::head:
      if (!route.query.empty()) break;
      handleHeadBucket(socket, store, version, route, yield);
      return;
    case http::verb::delete_:
      if (!route.query.empty()) break;
      handleDeleteBucket(socket, store, version, route, yield);
      return;
    default:
      break;
    }
    writeMethodNotAllowed(socket, version, headOnly, yield);
    return;
  }

  if (request.method() == http::verb::post && route.query == uploadsQuery) {
    handleCreateMultipartUpload(socket, request.base(), store, version, route, yield);
    return;
  }
  if (request.method() == http::verb::get && hasQuerySelector(route.query, objectPartsQuery)) {
    handleListObjectParts(socket, request.base(), store, version, route, yield);
    return;
  }
  if (route.query.find("upload-id=") != std::string::npos) {
    if (request.method() == http::verb::get) {
      handleListParts(socket, store, version, route, yield);
      return;
    }
    if (request.method() == http::verb::put && route.query.find("part-number=") != std::string::npos) {
      if (request.base().find(copySourceHeader) != request.base().end())
        handleUploadPartCopy(socket, request.base(), store, version, route, yield);
      else
        handleUploadPart(socket, buffer, parser, store, route, yield);
      return;
    }
    if (request.method() == http::verb::post) {
      handleCompleteMultipartUpload(socket, buffer, parser, store, route, yield);
      return;
    }
    if (request.method() == http::verb::delete_) {
      handleAbortMultipartUpload(socket, store, version, route, yield);
      return;
    }
  }

  std::optional<extora::object_version_id> versionId;
  std::string queryError;
  if (!parseObjectVersionId(route.query, versionId, queryError)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_request", queryError, headOnly, yield);
    return;
  }

  switch (request.method()) {
  case http::verb::put:
    if (versionId.has_value()) break;
    if (request.base().find(copySourceHeader) != request.base().end())
      handleCopyObject(socket, request.base(), store, version, route, yield);
    else
      handlePutObject(socket, buffer, parser, store, route, yield);
    return;
  case http::verb::get:
    handleGetObject(socket, request.base(), store, version, route, versionId, yield);
    return;
  case http::verb::head:
    handleHeadObject(socket, request.base(), store, version, route, versionId, yield);
    return;
  case http::verb::delete_:
    handleDeleteObject(socket, store, version, route, versionId, yield);
    return;
  default:
    break;
  }
  writeMethodNotAllowed(socket, version, headOnly, yield);
}
void writeInvalidHttpRequest(Tcp::socket& socket, std::string_view message, net::yield_context yield)
{
  writeErrorResponse(socket, 11, http::status::bad_request, "invalid_http_request", message, false, yield);
}

} // namespace extoraHttpExample
