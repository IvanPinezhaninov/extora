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

#include "HttpRoute.h"

#include <cstddef>

#include "HttpRequestCommon.h"

namespace extoraHttpExample {

namespace {

namespace beast = boost::beast;

} // namespace

using requestDetail::percentDecode;
using requestDetail::toString;

Route parseRoute(beast::string_view target)
{
  Route route;
  if (target.empty() || target.front() != '/') {
    route.errorMessage = "request target must start with '/'";
    return route;
  }

  const std::size_t querySeparator = target.find('?');
  if (querySeparator != beast::string_view::npos) {
    route.query = toString(target.substr(querySeparator + 1));
    target = target.substr(0, querySeparator);
  }

  if (target == "/") {
    route.type = RouteType::root;
    return route;
  }
  if (target == "/favicon.ico") {
    route.type = RouteType::favicon;
    return route;
  }
  if (target == "/_extora/health") {
    route.type = RouteType::health;
    return route;
  }
  if (target == "/_extora/buckets") {
    route.type = RouteType::buckets;
    return route;
  }
  constexpr beast::string_view bucketUsagePrefix = "/_extora/buckets/";
  constexpr beast::string_view bucketUsageSuffix = "/usage";
  if (target.size() > bucketUsagePrefix.size() + bucketUsageSuffix.size() &&
      target.substr(0, bucketUsagePrefix.size()) == bucketUsagePrefix &&
      target.substr(target.size() - bucketUsageSuffix.size()) == bucketUsageSuffix) {
    const beast::string_view encodedBucket =
        target.substr(bucketUsagePrefix.size(), target.size() - bucketUsagePrefix.size() - bucketUsageSuffix.size());
    if (encodedBucket.find('/') != beast::string_view::npos || !percentDecode(encodedBucket, route.bucket.value)) {
      route.errorMessage = "invalid percent-encoded bucket name";
      return route;
    }
    route.type = RouteType::bucket_usage;
    return route;
  }
  if (target == "/_extora/reclamation") {
    route.type = RouteType::reclamation;
    return route;
  }
  if (target == "/_extora/compaction") {
    route.type = RouteType::compaction;
    return route;
  }

  target.remove_prefix(1);
  const std::size_t separator = target.find('/');
  const beast::string_view encodedBucket = target.substr(0, separator);
  if (encodedBucket.empty() || !percentDecode(encodedBucket, route.bucket.value)) {
    route.errorMessage = "invalid percent-encoded bucket name";
    return route;
  }

  if (separator == beast::string_view::npos) {
    route.type = RouteType::bucket;
    return route;
  }

  const beast::string_view encodedKey = target.substr(separator + 1);
  if (encodedKey.empty() || !percentDecode(encodedKey, route.key.value)) {
    route.errorMessage = "invalid percent-encoded object key";
    return route;
  }

  route.type = RouteType::object;
  return route;
}

} // namespace extoraHttpExample
