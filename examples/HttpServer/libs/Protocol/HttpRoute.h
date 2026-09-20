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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPROUTE_H
#define EXTORA_HTTP_EXAMPLE_HTTPROUTE_H

#include <cstdint>
#include <string>

#include <boost/beast/core/string.hpp>

#include <extora/bucket_types.h>
#include <extora/object_types.h>

namespace extoraHttpExample {

enum class RouteType : std::uint8_t {
  invalid,
  root,
  favicon,
  health,
  buckets,
  bucket_usage,
  reclamation,
  compaction,
  bucket,
  object
};

struct Route {
  RouteType type = RouteType::invalid;
  extora::bucket_name bucket;
  extora::object_key key;
  std::string query;
  std::string errorMessage;
};

Route parseRoute(boost::beast::string_view target);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPROUTE_H
