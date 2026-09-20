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

#include "segment_layout.h"

#include <cstdio>

namespace extora::core::segment_detail {

storage_error ensure_directory(const std::filesystem::path& path)
{
  std::error_code error_code;
  std::filesystem::create_directories(path, error_code);
  if (error_code)
    return storage_error{storage_error_code::backend_failure, "failed to create directory: " + error_code.message()};

  if (!std::filesystem::is_directory(path, error_code))
    return storage_error{storage_error_code::backend_failure, "path exists and is not a directory"};

  return {};
}

std::filesystem::path segment_path(const std::filesystem::path& segments_directory, std::uint64_t segment_id)
{
  char name[64];
  std::snprintf(name, sizeof(name), "%016llu.dat", static_cast<unsigned long long>(segment_id));
  return segments_directory / std::string{name};
}

std::filesystem::path segment_creation_path(const std::filesystem::path& segments_directory, std::uint64_t segment_id)
{
  std::filesystem::path path = segment_path(segments_directory, segment_id);
  path += ".creating";
  return path;
}

} // namespace extora::core::segment_detail
