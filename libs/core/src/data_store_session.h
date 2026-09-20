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

#ifndef EXTORA_CORE_DATA_STORE_SESSION_H
#define EXTORA_CORE_DATA_STORE_SESSION_H

#include <extora/core/object_data_store.h>

namespace extora::core {

class data_write_session {
public:
  explicit data_write_session(object_data_store& data_store)
    : m_data_store{data_store}
  {}

  ~data_write_session()
  {
    close();
  }

  data_write_session(const data_write_session&) = delete;
  data_write_session& operator=(const data_write_session&) = delete;

  storage_error open(const physical_extent& extent)
  {
    close();
    const storage_error error = m_data_store.begin_write(extent, m_handle);
    m_is_open = !failed(error);
    return error;
  }

  bool is_open() const
  {
    return m_is_open;
  }

  data_write_handle handle() const
  {
    return m_handle;
  }

  void close() noexcept
  {
    if (!m_is_open) return;

    m_data_store.finish_write(m_handle);
    m_handle = {};
    m_is_open = false;
  }

private:
  object_data_store& m_data_store;
  data_write_handle m_handle;
  bool m_is_open = false;
};

class data_read_session {
public:
  explicit data_read_session(object_data_store& data_store)
    : m_data_store{data_store}
  {}

  ~data_read_session()
  {
    close();
  }

  data_read_session(const data_read_session&) = delete;
  data_read_session& operator=(const data_read_session&) = delete;

  storage_error open(const physical_extent& extent)
  {
    close();
    const storage_error error = m_data_store.begin_read(extent, m_handle);
    m_is_open = !failed(error);
    return error;
  }

  bool is_open() const
  {
    return m_is_open;
  }

  data_read_handle handle() const
  {
    return m_handle;
  }

  void close() noexcept
  {
    if (!m_is_open) return;

    m_data_store.finish_read(m_handle);
    m_handle = {};
    m_is_open = false;
  }

private:
  object_data_store& m_data_store;
  data_read_handle m_handle;
  bool m_is_open = false;
};

} // namespace extora::core

#endif // EXTORA_CORE_DATA_STORE_SESSION_H
