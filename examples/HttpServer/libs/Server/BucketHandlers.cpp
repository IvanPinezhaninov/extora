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

#include "BucketHandlers.h"
#include "HttpHandlerSupport.h"
#include "HttpJson.h"
#include "HttpQuery.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;

} // namespace

void handleListBuckets(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                       net::yield_context yield)
{
  extora::list_buckets_options options;
  std::string errorMessage;
  if (!parseBucketListOptions(route.query, options, errorMessage)) {
    writeErrorResponse(socket, version, http::status::bad_request, "invalid_list_options", errorMessage, false, yield);
    return;
  }
  auto outcome = store.async_list_buckets(options, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, bucketsJson(outcome.value), yield);
}

void handleGetBucketVersioning(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                               net::yield_context yield)
{
  auto outcome = store.async_get_bucket_versioning(route.bucket, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, bucketVersioningJson(outcome.value), yield);
}

void handleGetBucketUsage(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                          net::yield_context yield)
{
  auto outcome = store.async_get_bucket_usage(route.bucket, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, bucketUsageJson(outcome.value), yield);
}

void handlePutBucketVersioning(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                               extora::bucket_versioning_configuration configuration, net::yield_context yield)
{
  auto outcome = store.async_set_bucket_versioning(route.bucket, configuration, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  const extora::bucket_versioning_status status = configuration == extora::bucket_versioning_configuration::enabled
                                                      ? extora::bucket_versioning_status::enabled
                                                      : extora::bucket_versioning_status::suspended;
  writeJsonResponse(socket, version, http::status::ok, bucketVersioningJson(status), yield);
}

void handleGetReclamationEstimate(Tcp::socket& socket, AsyncStore& store, unsigned version, net::yield_context yield)
{
  auto outcome = store.async_get_reclamation_estimate(yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, reclamationEstimateJson(outcome.value), yield);
}

void handleReclaimStorage(Tcp::socket& socket, AsyncStore& store, unsigned version, net::yield_context yield)
{
  auto outcome = store.async_reclaim_storage(yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, reclaimResultJson(outcome.value), yield);
}

void handleCompactStorage(Tcp::socket& socket, AsyncStore& store, unsigned version, net::yield_context yield)
{
  auto outcome = store.async_compact_storage(yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }

  writeJsonResponse(socket, version, http::status::ok, compactionResultJson(outcome.value), yield);
}

void handleCreateBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        net::yield_context yield)
{
  auto outcome = store.async_create_bucket(route.bucket, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeEmptyResponse(socket, version, http::status::created, yield);
}

void handleHeadBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                      net::yield_context yield)
{
  auto outcome = store.async_head_bucket(route.bucket, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, true, yield);
    return;
  }
  writeBucketInfoResponse(socket, version, outcome.value, yield);
}

void handleDeleteBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        net::yield_context yield)
{
  auto outcome = store.async_delete_bucket(route.bucket, yield);
  if (extora::failed(outcome.error)) {
    writeStorageError(socket, version, outcome.error, false, yield);
    return;
  }
  writeEmptyResponse(socket, version, http::status::no_content, yield);
}

} // namespace extoraHttpExample
