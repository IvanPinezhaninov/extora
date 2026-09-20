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

#ifndef EXTORA_STORAGE_ROOT_LOCK_H
#define EXTORA_STORAGE_ROOT_LOCK_H

#include <filesystem>

#include <extora/storage_error.h>

namespace extora {

class storage_root_lock {
public:
  explicit storage_root_lock(std::filesystem::path root_directory);
  ~storage_root_lock();

  storage_root_lock(const storage_root_lock&) = delete;
  storage_root_lock& operator=(const storage_root_lock&) = delete;

  storage_error acquire();

private:
  std::filesystem::path m_root_directory;
#if defined(_WIN32)
  void* m_handle = nullptr;
#else
  int m_descriptor = -1;
#endif // defined(_WIN32)
};

} // namespace extora

#endif // EXTORA_STORAGE_ROOT_LOCK_H
