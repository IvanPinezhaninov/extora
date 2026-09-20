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

#ifndef EXTORA_SEGMENT_DATA_SEGMENT_FILE_IO_H
#define EXTORA_SEGMENT_DATA_SEGMENT_FILE_IO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <extora/storage_error.h>

namespace extora::core::segment_detail {

storage_error make_errno_error(storage_error_code code, const char* prefix);

int create_read_write(const std::filesystem::path& path);

int open_read_write_existing(const std::filesystem::path& path);

int close_file(int fd);

int sync_file(int fd);

int sync_directory(const std::filesystem::path& path);

int file_size(int fd, std::uint64_t& size);

int resize_file(int fd, std::uint64_t size);

int allocate_file(int fd, std::uint64_t size);

storage_error write_at(int fd, const std::byte* data, std::size_t size, std::uint64_t offset);

storage_error read_at(int fd, std::byte* data, std::size_t size, std::uint64_t offset, std::size_t& bytes_read);

} // namespace extora::core::segment_detail

#endif // EXTORA_SEGMENT_DATA_SEGMENT_FILE_IO_H
