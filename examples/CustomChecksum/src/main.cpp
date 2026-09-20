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

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "ExampleSupport.h"

namespace {

constexpr std::string_view fnv1a64Algorithm = "fnv1a-64";
constexpr std::uint64_t fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t fnv1a64Prime = 1099511628211ull;

class Fnv1a64Hasher final : public extora::hasher {
public:
  extora::storage_error update(const std::byte* data, std::size_t size) override
  {
    for (std::size_t index = 0; index < size; ++index) {
      m_value ^= std::to_integer<std::uint8_t>(data[index]);
      m_value *= fnv1a64Prime;
    }
    return {};
  }

  extora::storage_error finish(std::string& value) override
  {
    constexpr char hexDigits[] = "0123456789abcdef";
    value.resize(16);
    for (std::size_t index = 0; index < value.size(); ++index) {
      const std::size_t shift = (value.size() - index - 1) * 4;
      value[index] = hexDigits[(m_value >> shift) & 0xf];
    }
    return {};
  }

private:
  std::uint64_t m_value = fnv1a64OffsetBasis;
};

class Fnv1a64Factory final : public extora::hasher_factory {
public:
  std::unique_ptr<extora::hasher> create_hasher(const extora::checksum_algorithm_name& checksumAlgorithm,
                                                extora::storage_error& error) override
  {
    if (checksumAlgorithm.value != fnv1a64Algorithm) {
      error = extora::make_error(extora::storage_error_code::unsupported_checksum_algorithm,
                                 "unsupported checksum algorithm: " + checksumAlgorithm.value);
      return {};
    }

    error = {};
    return std::make_unique<Fnv1a64Hasher>();
  }
};

extora::storage_error calculateChecksum(extora::hasher_factory& factory,
                                        const extora::checksum_algorithm_name& checksumAlgorithm,
                                        std::string_view content, std::string& value)
{
  extora::storage_error error;
  std::unique_ptr<extora::hasher> hasher = factory.create_hasher(checksumAlgorithm, error);
  if (extora::failed(error) || !hasher) return error;

  error = hasher->update(reinterpret_cast<const std::byte*>(content.data()), content.size());
  if (extora::failed(error)) return error;
  return hasher->finish(value);
}

} // namespace

int main()
{
  using namespace extoraExample;

  const std::shared_ptr<Fnv1a64Factory> hashFactory = std::make_shared<Fnv1a64Factory>();
  extora::object_store_options options;
  options.root_directory = "extora-custom-checksum-example-store";
  options.custom_hasher_factory = hashFactory;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error)) return printError("open data store", error);

  error = createExampleBucket(*store);
  if (failed(error)) return printError("create bucket", error);

  const std::string content = "Custom checksum extension point";
  std::string checksumValue;
  const extora::checksum_algorithm_name checksumAlgorithm{std::string{fnv1a64Algorithm}};
  error = calculateChecksum(*hashFactory, checksumAlgorithm, content, checksumValue);
  if (failed(error)) return printError("calculate checksum", error);

  extora::object_metadata metadata;
  metadata.content_type = "text/plain";

  extora::put_object_options putOptions;
  putOptions.expected_content_length = content.size();
  putOptions.expected_checksum = extora::object_checksum{checksumAlgorithm, checksumValue};

  MemoryReader reader{bytesFromString(content)};
  extora::put_object_result putResult;
  error = store->put_object(extora::bucket_name{"examples"}, extora::object_key{"custom-checksum.txt"}, reader,
                            metadata, putResult, putOptions);
  if (failed(error)) return printError("put object", error);

  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  MemoryWriter writer;
  extora::open_object_result getResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"custom-checksum.txt"}, writer,
                     getOptions, getResult);
  if (failed(error)) return printError("get verified object", error);

  std::cout << "Algorithm: " << getResult.object.checksum->checksum_algorithm.value << '\n'
            << "Checksum: " << getResult.object.checksum->value << '\n'
            << "Content: " << writer.text() << '\n'
            << "FNV-1a is used only to demonstrate the extension API; "
               "it is not a cryptographic hash.\n";
  return EXIT_SUCCESS;
}
