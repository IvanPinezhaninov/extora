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

#ifndef EXTORA_OBJECT_STREAM_H
#define EXTORA_OBJECT_STREAM_H

#include <cstddef>

#include <extora/export.h>
#include <extora/storage_error.h>

namespace extora {

/**
 * @brief Result of one @ref object_reader::read call.
 *
 * @ref bytes_read bytes at the start of the supplied buffer are valid even
 * when @ref error reports a terminal failure. Consumers must process those
 * bytes before handling the error. @ref end_of_stream is true only after a
 * successful final read. A reader must not report more bytes than requested.
 */
struct [[nodiscard]] object_read_result {
  /** @brief Bytes written to the buffer. */
  std::size_t bytes_read = 0;

  /** @brief Whether no more data remains. */
  bool end_of_stream = false;

  /** @brief @ref storage_error from the reader. */
  storage_error error;
};

/**
 * @brief Streaming object source.
 *
 * @ref object_reader::read must not keep the supplied buffer. Calls must not
 * overlap. A reader that has returned an error must keep returning a terminal
 * error or successful end-of-stream result without producing more bytes.
 */
class EXTORA_API object_reader {
public:
  /** @brief Constructs the reader interface. */
  object_reader() noexcept;

  /** @brief Destroys the reader. */
  virtual ~object_reader() noexcept;

  /**
   * @brief Reads up to @p size bytes.
   *
   * @param[out] data Destination buffer.
   * @param[in]  size Buffer size.
   * @return @ref object_read_result satisfying its documented invariants.
   */
  virtual object_read_result read(std::byte* data, std::size_t size) = 0;
};

/**
 * @brief Streaming object sink.
 *
 * @ref object_writer::write must consume or copy the supplied bytes before
 * returning.
 */
class EXTORA_API object_writer {
public:
  /** @brief Constructs the writer interface. */
  object_writer() noexcept;

  /** @brief Destroys the writer. */
  virtual ~object_writer() noexcept;

  /**
   * @brief Writes @p size bytes.
   *
   * @param[in] data Source buffer.
   * @param[in] size Byte count.
   * @return @ref storage_error with the operation result.
   */
  virtual storage_error write(const std::byte* data, std::size_t size) = 0;
};

} // namespace extora

#endif // EXTORA_OBJECT_STREAM_H
