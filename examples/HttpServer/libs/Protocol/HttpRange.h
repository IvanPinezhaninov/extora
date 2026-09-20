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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPRANGE_H
#define EXTORA_HTTP_EXAMPLE_HTTPRANGE_H

#include <cstdint>
#include <optional>
#include <string_view>

#include <boost/beast/http/fields.hpp>

#include <extora/object_types.h>
#include <extora/storage_error.h>

namespace extoraHttpExample {

extora::storage_error parseByteRange(std::string_view value, extora::byte_range& range);

extora::storage_error parseRequestedRange(const boost::beast::http::fields& fields,
                                          std::optional<extora::byte_range>& range);

extora::storage_error parseRange(const boost::beast::http::fields& fields, std::uint64_t objectSize,
                                 std::optional<extora::byte_range>& range,
                                 std::optional<extora::resolved_byte_range>& resolved);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPRANGE_H
