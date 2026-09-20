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
#include <string_view>

#include "ExampleSupport.h"

namespace {

extora::storage_error putText(extora::object_store& store, std::string_view text, extora::put_object_result& result)
{
  extoraExample::MemoryReader reader{extoraExample::bytesFromString(text)};

  extora::object_metadata metadata;
  metadata.content_type = "text/plain";

  return store.put_object(extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, reader, metadata,
                          result);
}

} // namespace

int main()
{
  using namespace extoraExample;

  extora::object_store_options storeOptions;
  storeOptions.root_directory = "extora-versioning-example-store";

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(storeOptions, error);
  if (failed(error)) return printError("open data store", error);

  error = createExampleBucket(*store);
  if (failed(error)) return printError("create bucket", error);

  error =
      store->set_bucket_versioning(extora::bucket_name{"examples"}, extora::bucket_versioning_configuration::enabled);
  if (failed(error)) return printError("enable bucket versioning", error);

  extora::put_object_result firstPut;
  error = putText(*store, "first version", firstPut);
  if (failed(error)) return printError("put first version", error);

  extora::put_object_result secondPut;
  error = putText(*store, "second version", secondPut);
  if (failed(error)) return printError("put second version", error);

  extora::open_object_options firstReadOptions;
  firstReadOptions.version_id = firstPut.version_id;

  MemoryWriter firstWriter;
  extora::open_object_result firstReadResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, firstWriter,
                     firstReadOptions, firstReadResult);
  if (failed(error)) return printError("read first version", error);

  extora::delete_object_result deleteMarker;
  error = store->delete_object(extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, deleteMarker);
  if (failed(error)) return printError("create delete marker", error);

  MemoryWriter hiddenWriter;
  extora::open_object_result hiddenReadResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, hiddenWriter,
                     extora::open_object_options{}, hiddenReadResult);
  if (error.code != extora::storage_error_code::object_is_delete_marker) {
    if (failed(error)) return printError("read object hidden by delete marker", error);

    std::cerr << "Expected object_is_delete_marker after creating delete marker\n";
    return EXIT_FAILURE;
  }

  extora::list_object_versions_options listOptions;
  listOptions.prefix = "versioned.txt";

  extora::object_version_list versions;
  error = store->list_object_versions(extora::bucket_name{"examples"}, versions, listOptions);
  if (failed(error)) return printError("list object versions", error);

  std::cout << "First version ID: " << firstPut.version_id.value << '\n'
            << "Second version ID: " << secondPut.version_id.value << '\n'
            << "First version content: " << firstWriter.text() << '\n'
            << "Delete marker ID: " << deleteMarker.version_id.value << '\n'
            << "Latest object is hidden by the delete marker\n\n"
            << "Versions:\n";

  for (const extora::object_info& version : versions.versions) {
    std::cout << "  " << version.version_id.value;

    if (version.is_latest) std::cout << " [latest]";

    if (version.is_delete_marker)
      std::cout << " [delete marker]";
    else
      std::cout << " etag=" << version.etag << " size=" << version.content_length;

    std::cout << '\n';
  }

  extora::delete_object_options removeMarkerOptions;
  removeMarkerOptions.version_id = deleteMarker.version_id;

  extora::delete_object_result removedMarker;
  error = store->delete_object(extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, removedMarker,
                               removeMarkerOptions);
  if (failed(error)) return printError("remove delete marker", error);

  MemoryWriter restoredWriter;
  extora::open_object_result restoredReadResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"versioned.txt"}, restoredWriter,
                     extora::open_object_options{}, restoredReadResult);
  if (failed(error)) return printError("read restored latest version", error);

  std::cout << "\nLatest content after removing the delete marker: " << restoredWriter.text() << '\n';
  return EXIT_SUCCESS;
}
