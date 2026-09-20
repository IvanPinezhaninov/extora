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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPCONSTANTS_H
#define EXTORA_HTTP_EXAMPLE_HTTPCONSTANTS_H

#include <cstddef>
#include <string_view>

namespace extoraHttpExample {

inline constexpr char serverName[] = "Extora HTTP example";
inline constexpr char checksumAlgorithmHeader[] = "X-Extora-Checksum-Algorithm";
inline constexpr char checksumHeader[] = "X-Extora-Checksum";
inline constexpr char checksumTypeHeader[] = "X-Extora-Checksum-Type";
inline constexpr char copySourceHeader[] = "X-Extora-Copy-Source";
inline constexpr char copySourceConditionPrefix[] = "X-Extora-Copy-Source-";
inline constexpr char copySourceRangeHeader[] = "X-Extora-Copy-Source-Range";
inline constexpr char copySourceVersionIdHeader[] = "X-Extora-Copy-Source-Version-Id";
inline constexpr char customMetadataHeaderPrefix[] = "X-Extora-Meta-";
inline constexpr char deleteMarkerHeader[] = "X-Extora-Delete-Marker";
inline constexpr char metadataDirectiveHeader[] = "X-Extora-Metadata-Directive";
inline constexpr char versionIdHeader[] = "X-Extora-Version-Id";
inline constexpr char bucketCreatedAtHeader[] = "X-Extora-Bucket-Created-At-Ms";
inline constexpr char bucketVersioningHeader[] = "X-Extora-Bucket-Versioning";
inline constexpr std::string_view versionsQuery = "versions";
inline constexpr std::string_view uploadsQuery = "uploads";
inline constexpr std::string_view objectPartsQuery = "object-parts";
inline constexpr std::size_t completionBodyLimit = 4 * 1024 * 1024;

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPCONSTANTS_H
