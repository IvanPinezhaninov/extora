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

#ifndef EXTORA_TEST_PUBLIC_TEST_SUPPORT_H
#define EXTORA_TEST_PUBLIC_TEST_SUPPORT_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <extora/extora.h>

namespace extoraTest {

std::string makeTempRoot();

std::string joinPath(const std::filesystem::path& parent, const std::filesystem::path& child);

bool regularFileExists(const std::filesystem::path& path);

bool overwriteFileByte(const std::filesystem::path& path, long offset, std::byte value);

std::vector<std::byte> bytesFromString(std::string_view text);

std::string stringFromBytes(const std::vector<std::byte>& bytes);

class VectorReader final : public extora::object_reader {
public:
  VectorReader(std::vector<std::byte> data, std::size_t chunkSize);

  extora::object_read_result read(std::byte* data, std::size_t size) override;

private:
  std::vector<std::byte> m_data;
  std::size_t m_chunkSize = 0;
  std::size_t m_offset = 0;
};

class VectorWriter final : public extora::object_writer {
public:
  extora::storage_error write(const std::byte* data, std::size_t size) override;

  const std::vector<std::byte>& bytes() const;

private:
  std::vector<std::byte> m_bytes;
};

extora::storage_error readObject(extora::object_store& store, const extora::bucket_name& bucket,
                                 const extora::object_key& key, extora::object_writer& writer,
                                 const extora::open_object_options& options, extora::open_object_result& result);

extora::open_object_result& ignoredOpenObjectResult();

extora::put_object_result& ignoredPutResult();

extora::delete_object_result& ignoredDeleteResult();

extora::upload_part_result& ignoredUploadPartResult();

extora::reclaim_storage_result& ignoredReclaimResult();

} // namespace extoraTest

#endif // EXTORA_TEST_PUBLIC_TEST_SUPPORT_H
