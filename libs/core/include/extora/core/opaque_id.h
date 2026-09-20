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

#ifndef EXTORA_CORE_OPAQUE_ID_H
#define EXTORA_CORE_OPAQUE_ID_H

#include <chrono>
#include <string>
#include <string_view>

namespace extora::core {

inline constexpr char object_version_id_prefix = 'v';
inline constexpr char delete_marker_version_id_prefix = 'd';
inline constexpr char multipart_upload_id_prefix = 'u';
inline constexpr char write_operation_id_prefix = 'w';
inline constexpr char bucket_listing_token_prefix = 'b';
inline constexpr char object_listing_token_prefix = 'o';

std::string generate_opaque_id(char prefix);

std::string generate_ordered_opaque_id(char prefix, std::chrono::system_clock::time_point time);

std::string encode_listing_token(char prefix, std::string_view marker);

bool decode_listing_token(std::string_view token, char prefix, std::string& marker);

} // namespace extora::core

#endif // EXTORA_CORE_OPAQUE_ID_H
