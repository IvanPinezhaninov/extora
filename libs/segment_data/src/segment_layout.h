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

#ifndef EXTORA_SEGMENT_DATA_SEGMENT_LAYOUT_H
#define EXTORA_SEGMENT_DATA_SEGMENT_LAYOUT_H

#include <cstdint>
#include <filesystem>

#include <extora/storage_error.h>

namespace extora::core::segment_detail {

storage_error ensure_directory(const std::filesystem::path& path);

std::filesystem::path segment_path(const std::filesystem::path& segments_directory, std::uint64_t segment_id);

std::filesystem::path segment_creation_path(const std::filesystem::path& segments_directory, std::uint64_t segment_id);

} // namespace extora::core::segment_detail

#endif // EXTORA_SEGMENT_DATA_SEGMENT_LAYOUT_H
