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

#ifndef EXTORA_HTTP_EXAMPLE_PROTOCOL_H
#define EXTORA_HTTP_EXAMPLE_PROTOCOL_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <extora/storage_error.h>

namespace boost::beast::http {
enum class status : unsigned;
} // namespace boost::beast::http

namespace extoraHttpExample {

const char* storageErrorCodeName(extora::storage_error_code code);

boost::beast::http::status statusForStorageError(extora::storage_error_code code);

std::optional<std::string> formatHttpDate(std::chrono::system_clock::time_point value);

bool isHttpToken(std::string_view value);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_PROTOCOL_H
