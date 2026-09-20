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

#ifndef EXTORA_ASIO_ASYNC_OBJECT_STORE_H
#define EXTORA_ASIO_ASYNC_OBJECT_STORE_H

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>

#include <extora/managed_object_store.h>

namespace extora::asio {

/** @brief Result delivered by an asynchronous storage operation. */
template<typename T>
struct [[nodiscard]] operation_result {
  /** @brief @ref storage_error from the operation. */
  storage_error error;

  /** @brief Operation value, valid according to the synchronous API contract. */
  T value{};
};

/** @brief Result delivered by an asynchronous operation without an output value. */
template<>
struct [[nodiscard]] operation_result<void> {
  /** @brief @ref storage_error from the operation. */
  storage_error error;
};

namespace detail {

template<typename Result>
class operation_initiation {
public:
  using executor_type = boost::asio::any_io_executor;

  operation_initiation(executor_type completion_executor, executor_type blocking_executor)
    : m_completion_executor{std::move(completion_executor)}
    , m_blocking_executor{std::move(blocking_executor)}
  {}

  [[nodiscard]] executor_type get_executor() const noexcept
  {
    return m_completion_executor;
  }

  template<typename CompletionHandler, typename Operation>
  void operator()(CompletionHandler&& handler, Operation&& operation) const
  {
    auto completion_allocator = boost::asio::get_associated_allocator(handler);
    auto completion_executor = boost::asio::get_associated_executor(handler, m_completion_executor);
    auto completion_work = boost::asio::make_work_guard(completion_executor);
    boost::asio::post(
        m_blocking_executor,
        boost::asio::bind_allocator(completion_allocator, [completion_allocator = std::move(completion_allocator),
                                                           completion_executor = std::move(completion_executor),
                                                           handler = std::forward<CompletionHandler>(handler),
                                                           operation = std::forward<Operation>(operation),
                                                           completion_work = std::move(completion_work)]() mutable {
          operation_result<Result> result;
          try {
            if constexpr (std::is_void<Result>::value)
              result.error = operation();
            else
              result.error = operation(result.value);
          } catch (const std::exception& exception) {
            result.error = make_error(storage_error_code::backend_failure,
                                      "asynchronous storage operation failed: " + std::string{exception.what()});
          } catch (...) {
            result.error = make_error(storage_error_code::backend_failure, "asynchronous storage operation failed");
          }

          boost::asio::post(completion_executor,
                            boost::asio::bind_allocator(completion_allocator,
                                                        [handler = std::move(handler), result = std::move(result),
                                                         completion_work = std::move(completion_work)]() mutable {
                                                          std::move(handler)(std::move(result));
                                                        }));
        }));
  }

private:
  executor_type m_completion_executor;
  executor_type m_blocking_executor;
};

} // namespace detail

/**
 * @brief Boost.Asio adapter for the synchronous managed object store.
 *
 * Initiating functions copy their value arguments before returning, execute
 * the synchronous operation on @p blocking_executor, and deliver completion
 * through the completion token's associated executor. Outstanding operations
 * retain the store. Referenced buffers, readers, and writers must outlive their
 * operations.
 */
class async_object_store {
public:
  using executor_type = boost::asio::any_io_executor;

  async_object_store(std::shared_ptr<managed_object_store> store, executor_type completion_executor,
                     executor_type blocking_executor);

  [[nodiscard]] executor_type get_executor() const noexcept;

  template<typename CompletionToken>
  auto async_create_bucket(bucket_name bucket, CompletionToken&& token)
  {
    return async_submit<void>([store = m_store, bucket = std::move(bucket)] { return store->create_bucket(bucket); },
                              std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_delete_bucket(bucket_name bucket, CompletionToken&& token)
  {
    return async_submit<void>([store = m_store, bucket = std::move(bucket)] { return store->delete_bucket(bucket); },
                              std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_head_bucket(bucket_name bucket, CompletionToken&& token)
  {
    return async_submit<bucket_info>([store = m_store, bucket = std::move(bucket)](
                                         bucket_info& result) { return store->head_bucket(bucket, result); },
                                     std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_buckets(CompletionToken&& token)
  {
    return async_list_buckets(list_buckets_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_buckets(list_buckets_options options, CompletionToken&& token)
  {
    return async_submit<bucket_list>([store = m_store, options = std::move(options)](
                                         bucket_list& result) { return store->list_buckets(result, options); },
                                     std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_set_bucket_versioning(bucket_name bucket, bucket_versioning_configuration configuration,
                                   CompletionToken&& token)
  {
    return async_submit<void>([store = m_store, bucket = std::move(bucket),
                               configuration] { return store->set_bucket_versioning(bucket, configuration); },
                              std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_get_bucket_versioning(bucket_name bucket, CompletionToken&& token)
  {
    return async_submit<bucket_versioning_status>(
        [store = m_store, bucket = std::move(bucket)](bucket_versioning_status& result) {
          return store->get_bucket_versioning(bucket, result);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_put_object(bucket_name bucket, object_key key, object_reader& reader, object_metadata metadata,
                        CompletionToken&& token)
  {
    return async_put_object(std::move(bucket), std::move(key), reader, std::move(metadata), put_object_options{},
                            std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_put_object(bucket_name bucket, object_key key, object_reader& reader, object_metadata metadata,
                        put_object_options options, CompletionToken&& token)
  {
    return async_submit<put_object_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), &reader, metadata = std::move(metadata),
         options = std::move(options)](put_object_result& result) {
          return store->put_object(bucket, key, reader, metadata, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_create_multipart_upload(bucket_name bucket, object_key key, object_metadata metadata,
                                     CompletionToken&& token)
  {
    return async_create_multipart_upload(std::move(bucket), std::move(key), std::move(metadata),
                                         create_multipart_upload_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_create_multipart_upload(bucket_name bucket, object_key key, object_metadata metadata,
                                     create_multipart_upload_options options, CompletionToken&& token)
  {
    return async_submit<create_multipart_upload_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), metadata = std::move(metadata),
         options = std::move(options)](create_multipart_upload_result& result) {
          return store->create_multipart_upload(bucket, key, metadata, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_upload_part(bucket_name bucket, object_key key, multipart_upload_id upload_id, std::uint32_t part_number,
                         object_reader& reader, CompletionToken&& token)
  {
    return async_upload_part(std::move(bucket), std::move(key), std::move(upload_id), part_number, reader,
                             upload_part_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_upload_part(bucket_name bucket, object_key key, multipart_upload_id upload_id, std::uint32_t part_number,
                         object_reader& reader, upload_part_options options, CompletionToken&& token)
  {
    return async_submit<upload_part_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), upload_id = std::move(upload_id),
         part_number, &reader, options = std::move(options)](upload_part_result& result) {
          return store->upload_part(bucket, key, upload_id, part_number, reader, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_complete_multipart_upload(bucket_name bucket, object_key key, multipart_upload_id upload_id,
                                       complete_multipart_upload_options options, CompletionToken&& token)
  {
    return async_submit<put_object_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), upload_id = std::move(upload_id),
         options = std::move(options)](put_object_result& result) {
          return store->complete_multipart_upload(bucket, key, upload_id, options, result);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_abort_multipart_upload(bucket_name bucket, object_key key, multipart_upload_id upload_id,
                                    CompletionToken&& token)
  {
    return async_submit<void>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), upload_id = std::move(upload_id)] {
          return store->abort_multipart_upload(bucket, key, upload_id);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_parts(bucket_name bucket, object_key key, multipart_upload_id upload_id, CompletionToken&& token)
  {
    return async_list_parts(std::move(bucket), std::move(key), std::move(upload_id), list_parts_options{},
                            std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_parts(bucket_name bucket, object_key key, multipart_upload_id upload_id, list_parts_options options,
                        CompletionToken&& token)
  {
    return async_submit<multipart_part_list>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), upload_id = std::move(upload_id),
         options = std::move(options)](multipart_part_list& result) {
          return store->list_parts(bucket, key, upload_id, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_multipart_uploads(bucket_name bucket, CompletionToken&& token)
  {
    return async_list_multipart_uploads(std::move(bucket), list_multipart_uploads_options{},
                                        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_multipart_uploads(bucket_name bucket, list_multipart_uploads_options options, CompletionToken&& token)
  {
    return async_submit<multipart_upload_list>(
        [store = m_store, bucket = std::move(bucket), options = std::move(options)](multipart_upload_list& result) {
          return store->list_multipart_uploads(bucket, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_copy_object(bucket_name source_bucket, object_key source_key, bucket_name target_bucket,
                         object_key target_key, CompletionToken&& token)
  {
    return async_copy_object(std::move(source_bucket), std::move(source_key), std::move(target_bucket),
                             std::move(target_key), copy_object_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_copy_object(bucket_name source_bucket, object_key source_key, bucket_name target_bucket,
                         object_key target_key, copy_object_options options, CompletionToken&& token)
  {
    return async_submit<copy_object_result>(
        [store = m_store, source_bucket = std::move(source_bucket), source_key = std::move(source_key),
         target_bucket = std::move(target_bucket), target_key = std::move(target_key),
         options = std::move(options)](copy_object_result& result) {
          return store->copy_object(source_bucket, source_key, target_bucket, target_key, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_open_object(bucket_name bucket, object_key key, CompletionToken&& token)
  {
    return async_open_object(std::move(bucket), std::move(key), open_object_options{},
                             std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_open_object(bucket_name bucket, object_key key, open_object_options options, CompletionToken&& token)
  {
    return async_submit<open_object_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), options = std::move(options)](
            open_object_result& result) { return store->open_object(bucket, key, result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_head_object(bucket_name bucket, object_key key, CompletionToken&& token)
  {
    return async_head_object(std::move(bucket), std::move(key), head_object_options{},
                             std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_head_object(bucket_name bucket, object_key key, head_object_options options, CompletionToken&& token)
  {
    return async_submit<object_info>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), options = std::move(options)](
            object_info& result) { return store->head_object(bucket, key, result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_object_parts(bucket_name bucket, object_key key, CompletionToken&& token)
  {
    return async_list_object_parts(std::move(bucket), std::move(key), list_object_parts_options{},
                                   std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_object_parts(bucket_name bucket, object_key key, list_object_parts_options options,
                               CompletionToken&& token)
  {
    return async_submit<object_part_list>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), options = std::move(options)](
            object_part_list& result) { return store->list_object_parts(bucket, key, result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_delete_object(bucket_name bucket, object_key key, CompletionToken&& token)
  {
    return async_delete_object(std::move(bucket), std::move(key), delete_object_options{},
                               std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_delete_object(bucket_name bucket, object_key key, delete_object_options options, CompletionToken&& token)
  {
    return async_submit<delete_object_result>(
        [store = m_store, bucket = std::move(bucket), key = std::move(key), options = std::move(options)](
            delete_object_result& result) { return store->delete_object(bucket, key, result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_objects(bucket_name bucket, CompletionToken&& token)
  {
    return async_list_objects(std::move(bucket), list_objects_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_objects(bucket_name bucket, list_objects_options options, CompletionToken&& token)
  {
    return async_submit<object_list>([store = m_store, bucket = std::move(bucket), options = std::move(options)](
                                         object_list& result) { return store->list_objects(bucket, result, options); },
                                     std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_object_versions(bucket_name bucket, CompletionToken&& token)
  {
    return async_list_object_versions(std::move(bucket), list_object_versions_options{},
                                      std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_list_object_versions(bucket_name bucket, list_object_versions_options options, CompletionToken&& token)
  {
    return async_submit<object_version_list>(
        [store = m_store, bucket = std::move(bucket), options = std::move(options)](object_version_list& result) {
          return store->list_object_versions(bucket, result, options);
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_get_bucket_usage(bucket_name bucket, CompletionToken&& token)
  {
    return async_submit<bucket_usage>([store = m_store, bucket = std::move(bucket)](
                                          bucket_usage& result) { return store->get_bucket_usage(bucket, result); },
                                      std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_get_reclamation_estimate(CompletionToken&& token)
  {
    return async_submit<reclamation_estimate>(
        [store = m_store](reclamation_estimate& result) { return store->get_reclamation_estimate(result); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_reclaim_storage(CompletionToken&& token)
  {
    return async_reclaim_storage(reclaim_storage_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_reclaim_storage(reclaim_storage_options options, CompletionToken&& token)
  {
    return async_submit<reclaim_storage_result>(
        [store = m_store, options](reclaim_storage_result& result) { return store->reclaim_storage(result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_compact_storage(CompletionToken&& token)
  {
    return async_compact_storage(compact_storage_options{}, std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_compact_storage(compact_storage_options options, CompletionToken&& token)
  {
    return async_submit<compact_storage_result>(
        [store = m_store, options](compact_storage_result& result) { return store->compact_storage(result, options); },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_read_some(object_reader& reader, std::byte* data, std::size_t size, CompletionToken&& token)
  {
    return async_submit<object_read_result>(
        [&reader, data, size](object_read_result& result) {
          result = reader.read(data, size);
          return storage_error{};
        },
        std::forward<CompletionToken>(token));
  }

  template<typename CompletionToken>
  auto async_write_some(object_writer& writer, const std::byte* data, std::size_t size, CompletionToken&& token)
  {
    return async_submit<void>([&writer, data, size] { return writer.write(data, size); },
                              std::forward<CompletionToken>(token));
  }

private:
  template<typename Result, typename Operation, typename CompletionToken>
  auto async_submit(Operation&& operation, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(operation_result<Result>)>(
        detail::operation_initiation<Result>{m_completion_executor, m_blocking_executor}, token,
        std::forward<Operation>(operation));
  }

  std::shared_ptr<managed_object_store> m_store;
  executor_type m_completion_executor;
  executor_type m_blocking_executor;
};

} // namespace extora::asio

#endif // EXTORA_ASIO_ASYNC_OBJECT_STORE_H
