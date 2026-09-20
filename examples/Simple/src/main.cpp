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

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "ExampleSupport.h"

int main()
{
  using namespace extoraExample;

  extora::object_store_options options;
  options.root_directory = "extora-simple-example-store";

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error)) return printError("open data store", error);

  error = createExampleBucket(*store);
  if (failed(error)) return printError("create bucket", error);

  const std::string content = "Hello World!";
  MemoryReader reader{bytesFromString(content)};

  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  metadata.custom_metadata.push_back({"example", "simple"});

  extora::put_object_result putResult;
  error =
      store->put_object(extora::bucket_name{"examples"}, extora::object_key{"hello.txt"}, reader, metadata, putResult);
  if (failed(error)) return printError("put object", error);

  MemoryWriter writer;
  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  extora::open_object_result getResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"hello.txt"}, writer, getOptions,
                     getResult);
  if (failed(error)) return printError("get object", error);

  extora::object_info info;
  error = store->head_object(extora::bucket_name{"examples"}, extora::object_key{"hello.txt"}, info);
  if (failed(error)) return printError("stat object", error);

  if (info.etag != putResult.etag) {
    std::cerr << "PUT and stat returned different ETags\n";
    return EXIT_FAILURE;
  }
  if (!info.checksum.has_value() ||
      info.checksum->checksum_algorithm.value != putResult.checksum.checksum_algorithm.value ||
      info.checksum->value != putResult.checksum.value) {
    std::cerr << "PUT and stat returned different checksums\n";
    return EXIT_FAILURE;
  }

  std::cout << "Object: " << info.key.value << '\n'
            << "Size: " << info.content_length << " bytes\n"
            << "Content type: " << *info.content_type << '\n'
            << "ETag: " << info.etag << '\n'
            << "Checksum: " << info.checksum->checksum_algorithm.value << ':' << info.checksum->value << '\n'
            << "Created at: " << formatTime(info.created_at) << '\n'
            << "Modified at: " << formatTime(info.modified_at) << '\n'
            << "Metadata: " << info.custom_metadata[0].name << '=' << info.custom_metadata[0].value << '\n'
            << "Content: " << writer.text() << '\n';

  return EXIT_SUCCESS;
}
