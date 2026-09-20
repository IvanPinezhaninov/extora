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

#include "extora/core/opaque_id.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace extora::core {

namespace {

std::uint64_t mix(std::uint64_t value)
{
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31u);
}

std::uint64_t make_process_nonce()
{
  std::random_device random;
  const std::uint64_t random_value =
      (static_cast<std::uint64_t>(random()) << 32u) ^ static_cast<std::uint64_t>(random());
  const std::uint64_t timestamp = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
  return mix(random_value ^ timestamp);
}

char hex_digit(std::uint64_t value)
{
  constexpr char digits[] = "0123456789abcdef";
  return digits[value & 0xfu];
}

int hex_value(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

void write_hex(std::uint64_t value, std::string& result, std::size_t offset)
{
  for (std::size_t index = 0; index < 16; ++index) {
    result[offset + 15 - index] = hex_digit(value);
    value >>= 4u;
  }
}

} // namespace

std::string generate_opaque_id(const char prefix)
{
  static const std::uint64_t process_nonce = make_process_nonce();
  static std::atomic<std::uint64_t> sequence{0};

  std::array<std::uint64_t, 2> parts{{process_nonce, ++sequence}};
  std::string result(34, '0');
  result[0] = prefix;
  result[1] = '_';
  for (std::size_t part = 0; part < parts.size(); ++part)
    write_hex(parts[part], result, 2 + part * 16);
  return result;
}

std::string generate_ordered_opaque_id(const char prefix, const std::chrono::system_clock::time_point time)
{
  static const std::uint64_t process_nonce = make_process_nonce();
  static std::atomic<std::uint64_t> sequence{0};
  const std::uint64_t timestamp = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count());

  std::string result(34, '0');
  result[0] = prefix;
  result[1] = '_';
  write_hex(timestamp, result, 2);
  write_hex(mix(process_nonce ^ ++sequence), result, 18);
  return result;
}

std::string encode_listing_token(const char prefix, std::string_view marker)
{
  std::string result(2 + marker.size() * 2, '0');
  result[0] = prefix;
  result[1] = '_';
  for (std::size_t index = 0; index < marker.size(); ++index) {
    const auto value = static_cast<unsigned char>(marker[index]);
    result[2 + index * 2] = hex_digit(value >> 4u);
    result[3 + index * 2] = hex_digit(value);
  }
  return result;
}

bool decode_listing_token(std::string_view token, const char prefix, std::string& marker)
{
  marker.clear();
  if (token.size() < 4 || token[0] != prefix || token[1] != '_' || token.size() % 2 != 0) return false;

  marker.reserve((token.size() - 2) / 2);
  for (std::size_t index = 2; index < token.size(); index += 2) {
    const int high = hex_value(token[index]);
    const int low = hex_value(token[index + 1]);
    if (high < 0 || low < 0) {
      marker.clear();
      return false;
    }
    marker.push_back(static_cast<char>((high << 4) | low));
  }
  return !marker.empty();
}

} // namespace extora::core
