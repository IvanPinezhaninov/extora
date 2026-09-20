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

#ifndef EXTORA_CORE_HASHING_OBJECT_WRITER_H
#define EXTORA_CORE_HASHING_OBJECT_WRITER_H

#include <extora/checksum.h>
#include <extora/object_stream.h>

namespace extora::core {

class hashing_object_writer final : public object_writer {
public:
  explicit hashing_object_writer(hasher& instance)
    : m_hasher{instance}
  {}

  storage_error write(const std::byte* data, std::size_t size) override
  {
    return m_hasher.update(data, size);
  }

private:
  hasher& m_hasher;
};

} // namespace extora::core

#endif // EXTORA_CORE_HASHING_OBJECT_WRITER_H
