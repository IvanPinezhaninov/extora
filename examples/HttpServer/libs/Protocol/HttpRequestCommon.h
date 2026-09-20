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

#ifndef EXTORA_HTTP_EXAMPLE_REQUESTCOMMON_H
#define EXTORA_HTTP_EXAMPLE_REQUESTCOMMON_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/beast/core/string.hpp>
#include <boost/beast/http/fields.hpp>

namespace extoraHttpExample::requestDetail {

struct QueryParameter {
  std::string name;
  std::string value;
};

std::string toString(boost::beast::string_view value);

bool percentDecode(boost::beast::string_view encoded, std::string& decoded);

bool parseUnsigned(std::string_view text, std::uint64_t& value);

bool parseQueryParameters(std::string_view query, std::vector<QueryParameter>& parameters, std::string& errorMessage);

bool parsePageSize(std::string_view value, std::string_view name, std::size_t& result, std::string& errorMessage);

std::optional<std::chrono::system_clock::time_point> parseHttpDate(std::string_view value);

bool entityTagListMatches(std::string_view value, std::string_view currentEtag, bool strongComparison);

std::optional<std::string> fieldValue(const boost::beast::http::fields& fields, boost::beast::http::field field);

std::optional<std::string> fieldValue(const boost::beast::http::fields& fields, std::string_view name);

bool startsWithIgnoreCase(boost::beast::string_view value, std::string_view prefix);

bool parseConditionEtag(std::string_view value, bool allowWildcard, std::string& etag);

} // namespace extoraHttpExample::requestDetail

#endif // EXTORA_HTTP_EXAMPLE_REQUESTCOMMON_H
