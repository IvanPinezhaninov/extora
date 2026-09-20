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

#include "segment_file_io.h"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif // defined(_WIN32)

namespace extora::core::segment_detail {

storage_error make_errno_error(storage_error_code code, const char* prefix)
{
  const int system_error = errno;
  storage_error error;
  if (system_error == ENOSPC)
    error.code = storage_error_code::insufficient_space;
  else if (system_error == EACCES || system_error == EPERM)
    error.code = storage_error_code::permission_denied;
#if defined(ECANCELED)
  else if (system_error == ECANCELED)
    error.code = storage_error_code::operation_cancelled;
#endif // defined(ECANCELED)
  else
    error.code = code;
  error.message = prefix;
  error.message += ": ";
#if defined(_WIN32)
  char message[256] = {};
  if (::strerror_s(message, sizeof(message), system_error) == 0)
    error.message += message;
  else
    error.message += "unknown system error";
#else
  error.message += std::strerror(system_error);
#endif // defined(_WIN32)
  return error;
}

#if defined(_WIN32)

namespace {

void set_errno_from_windows_error(DWORD error)
{
  switch (error) {
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    errno = EEXIST;
    break;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    errno = ENOENT;
    break;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
    errno = EACCES;
    break;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL:
    errno = ENOSPC;
    break;
  case ERROR_INVALID_HANDLE:
    errno = EBADF;
    break;
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
    errno = ENOMEM;
    break;
  default:
    errno = EIO;
    break;
  }
}

class thread_io_event {
public:
  thread_io_event()
    : m_handle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)}
  {
    if (m_handle == nullptr) m_error = ::GetLastError();
  }

  ~thread_io_event()
  {
    if (m_handle != nullptr) ::CloseHandle(m_handle);
  }

  HANDLE handle() const
  {
    return m_handle;
  }

  DWORD error() const
  {
    return m_error;
  }

private:
  HANDLE m_handle = nullptr;
  DWORD m_error = ERROR_SUCCESS;
};

bool initialize_overlapped(std::uint64_t offset, OVERLAPPED& overlapped)
{
  thread_local thread_io_event event;
  const HANDLE event_handle = event.handle();
  if (event_handle == nullptr) {
    set_errno_from_windows_error(event.error());
    return false;
  }
  if (::ResetEvent(event_handle) == 0) {
    set_errno_from_windows_error(::GetLastError());
    return false;
  }

  overlapped.Offset = static_cast<DWORD>(offset);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32u);
  overlapped.hEvent = event_handle;
  return true;
}

DWORD complete_io(HANDLE handle, BOOL started, OVERLAPPED& overlapped, DWORD& transferred)
{
  if (started == 0) {
    const DWORD error = ::GetLastError();
    if (error != ERROR_IO_PENDING) return error;
  }

  if (::GetOverlappedResult(handle, &overlapped, &transferred, TRUE) != 0) return ERROR_SUCCESS;

  return ::GetLastError();
}

int open_file(const std::filesystem::path& path, DWORD creation_disposition)
{
  const HANDLE handle =
      ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, creation_disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    set_errno_from_windows_error(::GetLastError());
    return -1;
  }

  const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY | _O_NOINHERIT);
  if (fd >= 0) return fd;

  const int system_error = errno;
  ::CloseHandle(handle);
  errno = system_error;
  return -1;
}

int set_file_size(HANDLE handle, std::uint64_t size)
{
  LARGE_INTEGER file_position;
  file_position.QuadPart = static_cast<LONGLONG>(size);
  if (::SetFilePointerEx(handle, file_position, nullptr, FILE_BEGIN) == 0 || ::SetEndOfFile(handle) == 0) {
    const DWORD error = ::GetLastError();
    errno = error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL ? ENOSPC : EIO;
    return -1;
  }

  return 0;
}

} // namespace

int create_read_write(const std::filesystem::path& path)
{
  return open_file(path, CREATE_NEW);
}

int open_read_write_existing(const std::filesystem::path& path)
{
  return open_file(path, OPEN_EXISTING);
}

int close_file(int fd)
{
  return ::_close(fd);
}

int sync_file(int fd)
{
  return ::_commit(fd);
}

int sync_directory(const std::filesystem::path& path)
{
  const HANDLE handle =
      ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    errno = EIO;
    return -1;
  }

  const BOOL result = ::FlushFileBuffers(handle);
  ::CloseHandle(handle);
  if (result == 0) errno = EIO;
  return result == 0 ? -1 : 0;
}

int file_size(int fd, std::uint64_t& size)
{
  struct _stat64 info;
  if (::_fstat64(fd, &info) != 0) return -1;

  size = static_cast<std::uint64_t>(info.st_size);
  return 0;
}

int resize_file(int fd, std::uint64_t size)
{
  const intptr_t os_handle = ::_get_osfhandle(fd);
  if (os_handle == -1) {
    errno = EBADF;
    return -1;
  }

  const HANDLE handle = reinterpret_cast<HANDLE>(os_handle);
  OVERLAPPED overlapped{};
  if (!initialize_overlapped(0, overlapped)) return -1;

  DWORD bytes_returned = 0;
  const BOOL started = ::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, nullptr, &overlapped);
  const DWORD sparse_error = complete_io(handle, started, overlapped, bytes_returned);
  if (sparse_error != ERROR_SUCCESS) {
    const DWORD error = sparse_error;
    if (error != ERROR_INVALID_FUNCTION && error != ERROR_NOT_SUPPORTED) {
      set_errno_from_windows_error(error);
      return -1;
    }
  }

  return set_file_size(handle, size);
}

int allocate_file(int fd, std::uint64_t size)
{
  const intptr_t os_handle = ::_get_osfhandle(fd);
  if (os_handle == -1) {
    errno = EBADF;
    return -1;
  }

  FILE_ALLOCATION_INFO allocation_info;
  allocation_info.AllocationSize.QuadPart = static_cast<LONGLONG>(size);

  const HANDLE handle = reinterpret_cast<HANDLE>(os_handle);
  if (::SetFileInformationByHandle(handle, FileAllocationInfo, &allocation_info, sizeof(allocation_info)) == 0) {
    const DWORD error = ::GetLastError();
    errno = error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL ? ENOSPC : EIO;
    return -1;
  }

  return set_file_size(handle, size);
}

bool is_interrupted()
{
  return false;
}

storage_error write_at(int fd, const std::byte* data, std::size_t size, std::uint64_t offset)
{
  const intptr_t os_handle = ::_get_osfhandle(fd);
  if (os_handle == -1) {
    errno = EBADF;
    return make_errno_error(storage_error_code::backend_failure, "failed to access segment handle");
  }

  std::size_t total_written = 0;
  while (total_written < size) {
    const std::uint64_t write_offset = offset + total_written;
    OVERLAPPED overlapped{};
    if (!initialize_overlapped(write_offset, overlapped))
      return make_errno_error(storage_error_code::backend_failure, "failed to prepare segment write");
    const DWORD requested =
        static_cast<DWORD>(std::min<std::size_t>(size - total_written, static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    const HANDLE handle = reinterpret_cast<HANDLE>(os_handle);
    const BOOL started = ::WriteFile(handle, data + total_written, requested, nullptr, &overlapped);
    const DWORD io_error = complete_io(handle, started, overlapped, written);
    if (io_error != ERROR_SUCCESS) {
      set_errno_from_windows_error(io_error);
      return make_errno_error(storage_error_code::backend_failure, "failed to write segment");
    }

    if (written == 0) return make_error(storage_error_code::backend_failure, "segment write made no progress");

    total_written += static_cast<std::size_t>(written);
  }

  return {};
}

storage_error read_at(int fd, std::byte* data, std::size_t size, std::uint64_t offset, std::size_t& bytes_read)
{
  bytes_read = 0;
  const intptr_t os_handle = ::_get_osfhandle(fd);
  if (os_handle == -1) {
    errno = EBADF;
    return make_errno_error(storage_error_code::backend_failure, "failed to access segment handle");
  }

  OVERLAPPED overlapped{};
  if (!initialize_overlapped(offset, overlapped))
    return make_errno_error(storage_error_code::backend_failure, "failed to prepare segment read");
  const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(size, static_cast<std::size_t>(MAXDWORD)));
  DWORD result = 0;
  const HANDLE handle = reinterpret_cast<HANDLE>(os_handle);
  const BOOL started = ::ReadFile(handle, data, requested, nullptr, &overlapped);
  const DWORD io_error = complete_io(handle, started, overlapped, result);
  if (io_error != ERROR_SUCCESS) {
    set_errno_from_windows_error(io_error);
    return make_errno_error(storage_error_code::backend_failure, "failed to read segment");
  }

  bytes_read = static_cast<std::size_t>(result);
  return {};
}

#else

namespace {

bool is_interrupted()
{
  return errno == EINTR;
}

} // namespace

int create_read_write(const std::filesystem::path& path)
{
  return ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
}

int open_read_write_existing(const std::filesystem::path& path)
{
  return ::open(path.c_str(), O_RDWR);
}

int close_file(int fd)
{
  return ::close(fd);
}

int sync_file(int fd)
{
  while (true) {
#if defined(__APPLE__)
    if (::fsync(fd) == 0) return 0;
#else
    if (::fdatasync(fd) == 0) return 0;
#endif // defined(__APPLE__)
    if (is_interrupted()) continue;
    return -1;
  }
}

int sync_directory(const std::filesystem::path& path)
{
#if defined(O_DIRECTORY)
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
#endif // defined(O_DIRECTORY)
  if (fd < 0) return -1;

  const int result = ::fsync(fd);
  const int sync_error = errno;
  ::close(fd);
  errno = sync_error;
  return result;
}

int file_size(int fd, std::uint64_t& size)
{
  struct stat info;
  if (::fstat(fd, &info) != 0) return -1;

  size = static_cast<std::uint64_t>(info.st_size);
  return 0;
}

int resize_file(int fd, std::uint64_t size)
{
  return ::ftruncate(fd, static_cast<off_t>(size));
}

int allocate_file(int fd, std::uint64_t size)
{
#if defined(__APPLE__)
  fstore_t store;
  std::memset(&store, 0, sizeof(store));
  store.fst_flags = F_ALLOCATECONTIG;
  store.fst_posmode = F_PEOFPOSMODE;
  store.fst_offset = 0;
  store.fst_length = static_cast<off_t>(size);

  if (::fcntl(fd, F_PREALLOCATE, &store) != 0) {
    store.fst_flags = F_ALLOCATEALL;
    if (::fcntl(fd, F_PREALLOCATE, &store) != 0) return -1;
  }

  return resize_file(fd, size);
#else
  while (true) {
    const int result = ::posix_fallocate(fd, 0, static_cast<off_t>(size));
    if (result == 0) return 0;
    if (result == EINTR) continue;

    errno = result;
    return -1;
  }
#endif // defined(__APPLE__)
}

storage_error write_at(int fd, const std::byte* data, std::size_t size, std::uint64_t offset)
{
  std::size_t total_written = 0;
  while (total_written < size) {
    const ssize_t result =
        ::pwrite(fd, data + total_written, size - total_written, static_cast<off_t>(offset + total_written));
    if (result < 0) {
      if (is_interrupted()) continue;
      return make_errno_error(storage_error_code::backend_failure, "failed to write segment");
    }

    if (result == 0) return make_error(storage_error_code::backend_failure, "segment write made no progress");

    total_written += static_cast<std::size_t>(result);
  }

  return {};
}

storage_error read_at(int fd, std::byte* data, std::size_t size, std::uint64_t offset, std::size_t& bytes_read)
{
  while (true) {
    const ssize_t result = ::pread(fd, data, size, static_cast<off_t>(offset));
    if (result < 0) {
      if (is_interrupted()) continue;
      return make_errno_error(storage_error_code::backend_failure, "failed to read segment");
    }

    bytes_read = static_cast<std::size_t>(result);
    return {};
  }
}

#endif // defined(_WIN32)

} // namespace extora::core::segment_detail
