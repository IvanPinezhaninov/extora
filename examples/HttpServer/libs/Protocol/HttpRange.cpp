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

#include "HttpRange.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include <boost/beast/http/field.hpp>

#include "HttpRequestCommon.h"

namespace extoraHttpExample {

namespace {

namespace http = boost::beast::http;

} // namespace

using requestDetail::parseUnsigned;
using requestDetail::toString;

extora::storage_error parseByteRange(std::string_view value, extora::byte_range& range)
{
  constexpr std::string_view prefix = "bytes=";
  if (value.size() <= prefix.size() || value.compare(0, prefix.size(), prefix) != 0 ||
      value.find(',') != std::string_view::npos)
    return extora::make_error(extora::storage_error_code::invalid_range, "only one bytes range is supported");

  const std::string_view specification = value.substr(prefix.size());
  const std::size_t dash = specification.find('-');
  if (dash == std::string_view::npos || specification.find('-', dash + 1) != std::string_view::npos)
    return extora::make_error(extora::storage_error_code::invalid_range, "invalid bytes range");

  const std::string_view first = specification.substr(0, dash);
  const std::string_view last = specification.substr(dash + 1);
  if (first.empty()) {
    std::uint64_t suffixLength = 0;
    if (!parseUnsigned(last, suffixLength) || suffixLength == 0)
      return extora::make_error(extora::storage_error_code::invalid_range, "invalid suffix range");
    range = extora::byte_range{extora::byte_range_type::suffix, 0, suffixLength};
    return {};
  }

  std::uint64_t offset = 0;
  if (!parseUnsigned(first, offset))
    return extora::make_error(extora::storage_error_code::invalid_range, "invalid range start");
  if (last.empty()) {
    range = extora::byte_range{extora::byte_range_type::offset_to_end, offset, 0};
    return {};
  }

  std::uint64_t lastOffset = 0;
  if (!parseUnsigned(last, lastOffset) || lastOffset < offset ||
      lastOffset - offset == (std::numeric_limits<std::uint64_t>::max)())
    return extora::make_error(extora::storage_error_code::invalid_range, "invalid range end");
  range = extora::byte_range{extora::byte_range_type::offset_length, offset, lastOffset - offset + 1};
  return {};
}

extora::storage_error parseRequestedRange(const http::fields& fields, std::optional<extora::byte_range>& range)
{
  range.reset();
  const auto position = fields.find(http::field::range);
  if (position == fields.end()) return {};

  extora::byte_range requested;
  const extora::storage_error error = parseByteRange(toString(position->value()), requested);
  if (extora::failed(error)) return error;
  range = requested;
  return {};
}

extora::storage_error parseRange(const http::fields& fields, std::uint64_t objectSize,
                                 std::optional<extora::byte_range>& range,
                                 std::optional<extora::resolved_byte_range>& resolved)
{
  resolved.reset();
  const extora::storage_error error = parseRequestedRange(fields, range);
  if (extora::failed(error)) return error;
  if (!range.has_value()) return {};
  if (objectSize == 0)
    return extora::make_error(extora::storage_error_code::invalid_range, "an empty object has no byte ranges");

  const extora::byte_range requested = *range;
  if (requested.type == extora::byte_range_type::suffix) {
    const std::uint64_t length = std::min(requested.length, objectSize);
    resolved = extora::resolved_byte_range{objectSize - length, length};
    return {};
  }

  if (requested.offset >= objectSize)
    return extora::make_error(extora::storage_error_code::invalid_range, "range starts past the object");

  if (requested.type == extora::byte_range_type::offset_to_end) {
    resolved = extora::resolved_byte_range{requested.offset, objectSize - requested.offset};
    return {};
  }

  const std::uint64_t length = std::min(requested.length, objectSize - requested.offset);
  range = extora::byte_range{extora::byte_range_type::offset_length, requested.offset, length};
  resolved = extora::resolved_byte_range{requested.offset, length};
  return {};
}

} // namespace extoraHttpExample
