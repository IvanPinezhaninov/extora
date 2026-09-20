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
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "ExampleSupport.h"

namespace {

extora::storage_error createBucket(extora::object_store& store, const char* name)
{
  extora::storage_error error = store.create_bucket(extora::bucket_name{name});
  if (error.code == extora::storage_error_code::bucket_already_exists) return {};
  return error;
}

} // namespace

int main()
{
  using namespace extoraExample;

  const std::filesystem::path root = "extora-object-lifecycle-example-store";
  std::error_code filesystemError;
  std::filesystem::remove_all(root, filesystemError);
  if (filesystemError) {
    std::cerr << "remove old example store failed: " << filesystemError.message() << '\n';
    return EXIT_FAILURE;
  }

  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024 * 1024;
  options.dedup_min_object_size = 1;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  if (failed(error)) return printError("open data store", error);

  error = createBucket(*store, "examples");
  if (failed(error)) return printError("create examples bucket", error);
  error = createBucket(*store, "archive");
  if (failed(error)) return printError("create archive bucket", error);
  extora::bucket_info examplesBucket;
  error = store->head_bucket(extora::bucket_name{"examples"}, examplesBucket);
  if (failed(error)) return printError("head examples bucket", error);

  extora::bucket_list buckets;
  error = store->list_buckets(buckets);
  if (failed(error)) return printError("list buckets", error);

  const std::string content = "Extora stores object data through streaming interfaces. "
                              "The object lifecycle example covers common storage operations.";
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  metadata.custom_metadata.push_back({"example", "object_lifecycle"});

  MemoryReader reader{bytesFromString(content)};
  extora::put_object_result putResult;
  error = store->put_object(extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, reader,
                            metadata, putResult);
  if (failed(error)) return printError("put object", error);

  MemoryReader duplicateReader{bytesFromString(content)};
  extora::put_object_result duplicateResult;
  error = store->put_object(extora::bucket_name{"examples"}, extora::object_key{"documents/deduplicated.txt"},
                            duplicateReader, metadata, duplicateResult);
  if (failed(error)) return printError("put deduplicated object", error);

  MemoryReader rejectedReader{bytesFromString(content)};
  extora::put_object_options rejectedPutOptions;
  rejectedPutOptions.conditions.if_none_match_etag = extora::etag_wildcard;
  extora::put_object_result rejectedPutResult;
  error = store->put_object(extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, rejectedReader,
                            metadata, rejectedPutResult, rejectedPutOptions);
  if (error.code != extora::storage_error_code::precondition_failed) {
    if (failed(error)) return printError("conditional put", error);
    std::cerr << "conditional put unexpectedly replaced the object\n";
    return EXIT_FAILURE;
  }

  extora::open_object_options readOptions;
  readOptions.conditions.if_match_etag = putResult.etag;
  MemoryWriter writer;
  extora::open_object_result getResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, writer,
                     readOptions, getResult);
  if (failed(error)) return printError("get conditional object", error);

  extora::open_object_options rangeOptions;
  rangeOptions.range = extora::byte_range{extora::byte_range_type::offset_length, 0, 32};
  MemoryWriter rangeWriter;
  extora::open_object_result rangeResult;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, rangeWriter,
                     rangeOptions, rangeResult);
  if (failed(error)) return printError("get object range", error);

  extora::copy_object_options copyOptions;
  copyOptions.source_conditions.if_match_etag = putResult.etag;
  extora::copy_object_result localCopyResult;
  error = store->copy_object(extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"},
                             extora::bucket_name{"examples"}, extora::object_key{"documents/archive/hello.txt"},
                             localCopyResult, copyOptions);
  if (failed(error)) return printError("copy object in bucket", error);

  copyOptions.replace_metadata = true;
  copyOptions.metadata.content_type = "text/plain";
  copyOptions.metadata.custom_metadata.push_back({"copied", "true"});
  extora::copy_object_result archiveCopyResult;
  error = store->copy_object(extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"},
                             extora::bucket_name{"archive"}, extora::object_key{"hello.txt"}, archiveCopyResult,
                             copyOptions);
  if (failed(error)) return printError("copy object across buckets", error);

  extora::list_objects_options pageOptions;
  pageOptions.prefix = "documents/";
  pageOptions.max_keys = 2;
  extora::object_list firstPage;
  error = store->list_objects(extora::bucket_name{"examples"}, firstPage, pageOptions);
  if (failed(error)) return printError("list first object page", error);
  if (!firstPage.is_truncated) {
    std::cerr << "first object page was expected to be truncated\n";
    return EXIT_FAILURE;
  }
  if (!firstPage.next_continuation_token.has_value()) {
    std::cerr << "truncated object page did not return a continuation token\n";
    return EXIT_FAILURE;
  }

  pageOptions.continuation_token = *firstPage.next_continuation_token;
  extora::object_list secondPage;
  error = store->list_objects(extora::bucket_name{"examples"}, secondPage, pageOptions);
  if (failed(error)) return printError("list second object page", error);

  extora::list_objects_options groupedOptions;
  groupedOptions.prefix = "documents/";
  groupedOptions.delimiter = "/";
  extora::object_list groupedObjects;
  error = store->list_objects(extora::bucket_name{"examples"}, groupedObjects, groupedOptions);
  if (failed(error)) return printError("list grouped objects", error);

  extora::object_info info;
  error = store->head_object(extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, info);
  if (failed(error)) return printError("head object", error);

  store.reset();
  store = extora::open_object_store(options, error);
  if (failed(error)) return printError("reopen data store", error);

  MemoryWriter reopenedWriter;
  error = readObject(*store, extora::bucket_name{"examples"}, extora::object_key{"documents/hello.txt"}, reopenedWriter,
                     extora::open_object_options{}, getResult);
  if (failed(error)) return printError("get object after reopen", error);
  if (reopenedWriter.text() != content) {
    std::cerr << "reopened store returned unexpected content\n";
    return EXIT_FAILURE;
  }

  const std::string temporaryContent = "temporary archive payload";
  MemoryReader temporaryReader{bytesFromString(temporaryContent)};
  extora::object_metadata temporaryMetadata;
  extora::put_object_options temporaryPutOptions;
  temporaryPutOptions.expected_content_length = temporaryContent.size();
  extora::put_object_result temporaryPutResult;
  error = store->put_object(extora::bucket_name{"archive"}, extora::object_key{"temporary.txt"}, temporaryReader,
                            temporaryMetadata, temporaryPutResult, temporaryPutOptions);
  if (failed(error)) return printError("put temporary archive object", error);

  extora::delete_object_result deleteResult;
  error = store->delete_object(extora::bucket_name{"archive"}, extora::object_key{"temporary.txt"}, deleteResult);
  if (failed(error)) return printError("delete temporary archive object", error);
  error = store->delete_object(extora::bucket_name{"archive"}, extora::object_key{"hello.txt"}, deleteResult);
  if (failed(error)) return printError("delete archive object", error);
  error = store->delete_bucket(extora::bucket_name{"archive"});
  if (failed(error)) return printError("delete archive bucket", error);

  extora::reclaim_storage_result reclaimResult;
  error = store->reclaim_storage(reclaimResult);
  if (failed(error)) return printError("reclaim storage", error);

  std::cout << "Buckets created: " << buckets.buckets.size() << '\n'
            << "Object: " << info.key.value << '\n'
            << "Size: " << info.content_length << " bytes\n"
            << "ETag: " << info.etag << '\n'
            << "Created at: " << formatTime(info.created_at) << '\n'
            << "Modified at: " << formatTime(info.modified_at) << '\n'
            << "Range: " << rangeWriter.text() << '\n'
            << "Paginated objects: " << firstPage.objects.size() + secondPage.objects.size() << '\n'
            << "Grouped objects: " << groupedObjects.objects.size() << '\n'
            << "Common prefixes: " << groupedObjects.common_prefixes.size() << '\n'
            << "Reclaimed: " << reclaimResult.reclaimed_bytes << " bytes\n"
            << "Still reclaimable: " << reclaimResult.remaining_reclaimable_bytes << " bytes\n"
            << "Content after reopen: " << reopenedWriter.text() << '\n';
  return EXIT_SUCCESS;
}
