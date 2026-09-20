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

#include "HttpRequestCommon.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/beast/http/field.hpp>

#include "extora/object_types.h"

namespace extoraHttpExample::requestDetail {

namespace {

namespace beast = boost::beast;
namespace http = beast::http;

} // namespace

std::string toString(beast::string_view value)
{
  return std::string{value.data(), value.size()};
}

int hexDigit(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool percentDecode(beast::string_view encoded, std::string& decoded)
{
  decoded.clear();
  decoded.reserve(encoded.size());

  for (std::size_t index = 0; index < encoded.size(); ++index) {
    const char value = encoded[index];
    if (value != '%') {
      decoded.push_back(value);
      continue;
    }

    if (index + 2 >= encoded.size()) return false;
    const int high = hexDigit(encoded[index + 1]);
    const int low = hexDigit(encoded[index + 2]);
    if (high < 0 || low < 0) return false;

    decoded.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }

  return true;
}

bool parseUnsigned(std::string_view text, std::uint64_t& value)
{
  if (text.empty()) return false;

  value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') return false;
    const std::uint64_t amount = static_cast<std::uint64_t>(digit - '0');
    if (value > ((std::numeric_limits<std::uint64_t>::max)() - amount) / 10) return false;
    value = value * 10 + amount;
  }
  return true;
}

bool parseQueryParameters(std::string_view query, std::vector<QueryParameter>& parameters, std::string& errorMessage)
{
  parameters.clear();
  std::size_t offset = 0;
  while (offset < query.size()) {
    const std::size_t separator = query.find('&', offset);
    const std::string_view parameter =
        query.substr(offset, separator == std::string_view::npos ? query.size() - offset : separator - offset);
    const std::size_t equals = parameter.find('=');
    if (parameter.empty() || equals == std::string_view::npos) {
      errorMessage = "query parameters must use name=value";
      return false;
    }

    QueryParameter decoded;
    if (!percentDecode(parameter.substr(0, equals), decoded.name) ||
        !percentDecode(parameter.substr(equals + 1), decoded.value)) {
      errorMessage = "invalid percent-encoded query parameter";
      return false;
    }
    const auto duplicate = std::find_if(parameters.begin(), parameters.end(),
                                        [&](const QueryParameter& existing) { return existing.name == decoded.name; });
    if (duplicate != parameters.end()) {
      errorMessage = "duplicate query parameter: " + decoded.name;
      return false;
    }
    parameters.push_back(std::move(decoded));

    if (separator == std::string_view::npos) break;
    offset = separator + 1;
  }
  return true;
}

bool parsePageSize(std::string_view value, std::string_view name, std::size_t& result, std::string& errorMessage)
{
  std::uint64_t parsed = 0;
  if (!parseUnsigned(value, parsed) || parsed > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
    errorMessage = std::string{name} + " must be a non-negative integer";
    return false;
  }
  result = static_cast<std::size_t>(parsed);
  return true;
}

std::string_view trim(std::string_view value)
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

bool isLeapYear(int year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int monthNumber(std::string_view name)
{
  constexpr std::array<std::string_view, 12> names = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const auto position = std::find(names.begin(), names.end(), name);
  return position == names.end() ? 0 : static_cast<int>(position - names.begin()) + 1;
}

bool parseFixedNumber(std::string_view value, std::size_t offset, std::size_t length, int& number)
{
  if (offset + length > value.size()) return false;

  number = 0;
  for (std::size_t index = offset; index < offset + length; ++index) {
    if (value[index] < '0' || value[index] > '9') return false;
    number = number * 10 + value[index] - '0';
  }
  return true;
}

std::int64_t daysBeforeYear(int year)
{
  const std::int64_t previousYear = static_cast<std::int64_t>(year) - 1;
  const std::int64_t previousEpochYear = 1969;
  return (previousYear * 365 + previousYear / 4 - previousYear / 100 + previousYear / 400) -
         (previousEpochYear * 365 + previousEpochYear / 4 - previousEpochYear / 100 + previousEpochYear / 400);
}

std::optional<std::chrono::system_clock::time_point> parseHttpDate(std::string_view value)
{
  value = trim(value);
  if (value.size() != 29 || value[3] != ',' || value[4] != ' ' || value[7] != ' ' || value[11] != ' ' ||
      value[16] != ' ' || value[19] != ':' || value[22] != ':' || value[25] != ' ' || value.substr(26) != "GMT")
    return std::nullopt;

  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parseFixedNumber(value, 5, 2, day) || !parseFixedNumber(value, 12, 4, year) ||
      !parseFixedNumber(value, 17, 2, hour) || !parseFixedNumber(value, 20, 2, minute) ||
      !parseFixedNumber(value, 23, 2, second))
    return std::nullopt;

  const int month = monthNumber(value.substr(8, 3));
  constexpr std::array<int, 12> daysPerMonth = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1970 || month == 0 || day < 1 || hour > 23 || minute > 59 || second > 59) return std::nullopt;

  const int monthDays = daysPerMonth[static_cast<std::size_t>(month - 1)] + (month == 2 && isLeapYear(year) ? 1 : 0);
  if (day > monthDays) return std::nullopt;

  std::int64_t days = daysBeforeYear(year);
  for (int currentMonth = 1; currentMonth < month; ++currentMonth) {
    days += daysPerMonth[static_cast<std::size_t>(currentMonth - 1)];
    if (currentMonth == 2 && isLeapYear(year)) ++days;
  }
  days += day - 1;

  const std::int64_t secondsSinceEpoch =
      days * 24 * 60 * 60 + static_cast<std::int64_t>(hour) * 60 * 60 + minute * 60 + second;
  const auto startOfSecond = std::chrono::system_clock::time_point{std::chrono::seconds{secondsSinceEpoch}};
  return startOfSecond + std::chrono::seconds{1} - std::chrono::system_clock::duration{1};
}

bool entityTagListMatches(std::string_view value, std::string_view currentEtag, bool strongComparison)
{
  value = trim(value);
  if (value == "*") return true;

  std::size_t offset = 0;
  while (offset < value.size()) {
    while (offset < value.size() && (value[offset] == ' ' || value[offset] == '\t'))
      ++offset;

    bool weak = false;
    if (value.substr(offset, 2) == "W/") {
      weak = true;
      offset += 2;
    }
    if (offset >= value.size() || value[offset] != '"') return false;

    const std::size_t endQuote = value.find('"', offset + 1);
    if (endQuote == std::string_view::npos) return false;
    if ((!strongComparison || !weak) && value.substr(offset + 1, endQuote - offset - 1) == currentEtag) return true;

    offset = endQuote + 1;
    while (offset < value.size() && (value[offset] == ' ' || value[offset] == '\t'))
      ++offset;
    if (offset == value.size()) break;
    if (value[offset] != ',') return false;
    ++offset;
  }
  return false;
}

std::optional<std::string> fieldValue(const http::fields& fields, http::field field)
{
  const auto position = fields.find(field);
  if (position == fields.end()) return std::nullopt;
  return toString(position->value());
}

std::optional<std::string> fieldValue(const http::fields& fields, std::string_view name)
{
  const auto position = fields.find(beast::string_view{name.data(), name.size()});
  if (position == fields.end()) return std::nullopt;
  return toString(position->value());
}

bool startsWithIgnoreCase(beast::string_view value, std::string_view prefix)
{
  if (value.size() < prefix.size()) return false;
  return beast::iequals(value.substr(0, prefix.size()), beast::string_view{prefix.data(), prefix.size()});
}

bool parseConditionEtag(std::string_view value, bool allowWildcard, std::string& etag)
{
  value = trim(value);
  if (allowWildcard && value == extora::etag_wildcard) {
    etag = extora::etag_wildcard;
    return true;
  }
  if (value.size() < 2 || value.front() != '"' || value.back() != '"' || value.find(',', 1) != std::string_view::npos)
    return false;

  etag.assign(value.data() + 1, value.size() - 2);
  return !etag.empty() && etag.find('"') == std::string::npos;
}

} // namespace extoraHttpExample::requestDetail
