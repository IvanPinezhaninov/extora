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

#include "storage_root_lock.h"

#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif // defined(_WIN32)

namespace extora {

namespace {

storage_error make_root_error(const std::error_code& error, const char* message)
{
  storage_error_code code = storage_error_code::backend_failure;
  if (error == std::errc::permission_denied) code = storage_error_code::permission_denied;
  return make_error(code, std::string{message} + ": " + error.message());
}

} // namespace

storage_root_lock::storage_root_lock(std::filesystem::path root_directory)
  : m_root_directory{std::move(root_directory)}
{}

storage_root_lock::~storage_root_lock()
{
#if defined(_WIN32)
  if (m_handle != nullptr) ::CloseHandle(static_cast<HANDLE>(m_handle));
#else
  if (m_descriptor >= 0) {
    ::flock(m_descriptor, LOCK_UN);
    ::close(m_descriptor);
  }
#endif // defined(_WIN32)
}

storage_error storage_root_lock::acquire()
{
  std::error_code error;
  std::filesystem::create_directories(m_root_directory, error);
  if (error) return make_root_error(error, "failed to create storage root directory");
  if (!std::filesystem::is_directory(m_root_directory, error)) {
    if (error) return make_root_error(error, "failed to inspect storage root directory");
    return make_error(storage_error_code::invalid_configuration, "storage root path is not a directory");
  }

  const std::filesystem::path lock_path = m_root_directory / ".extora.lock";
#if defined(_WIN32)
  const HANDLE handle = ::CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD system_error = ::GetLastError();
    if (system_error == ERROR_SHARING_VIOLATION || system_error == ERROR_LOCK_VIOLATION)
      return make_error(storage_error_code::storage_in_use, "storage root is already in use");
    if (system_error == ERROR_ACCESS_DENIED)
      return make_error(storage_error_code::permission_denied, "cannot lock storage root");
    return make_error(storage_error_code::backend_failure, "failed to open storage root lock");
  }
  m_handle = handle;
#else
  const int descriptor = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (descriptor < 0) {
    const std::error_code open_error{errno, std::generic_category()};
    return make_root_error(open_error, "failed to open storage root lock");
  }
  if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
    const int lock_error = errno;
    ::close(descriptor);
    if (lock_error == EWOULDBLOCK || lock_error == EAGAIN)
      return make_error(storage_error_code::storage_in_use, "storage root is already in use");
    return make_root_error(std::error_code{lock_error, std::generic_category()}, "failed to lock storage root");
  }
  m_descriptor = descriptor;
#endif // defined(_WIN32)

  return {};
}

} // namespace extora
