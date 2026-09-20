# Boost.Asio adapter

The optional static `extora::asio` target adapts the synchronous
`managed_object_store` API to Boost.Asio CompletionToken initiating functions.
Enable it with:

```sh
cmake -S . -B build/asio -DEXTORA_BUILD_ASIO_ADAPTER=ON
```

Construct `extora::asio::async_object_store` with separate completion and
blocking executors. Value arguments are copied before an initiating function
returns. Outstanding operations retain the wrapped store. Buffers, readers,
and writers must outlive their outstanding operations.

Each completion receives an `extora::asio::operation_result<T>`, preserving
the structured `storage_error` and any operation value. Callback tokens,
`boost::asio::use_future`, and `boost::asio::yield_context` are supported by
the same API.

The adapter moves synchronous storage work onto the blocking executor. It does
not require an asynchronous producer to block an I/O thread:
`buffered_object_reader` provides a bounded, thread-safe bridge with explicit
backpressure. Producers submit owned chunks with `async_write`, terminate the
stream with `async_finish`, and run the synchronous storage consumer on the
blocking executor.

`async_admission_limiter` provides optional bounded admission before starting
storage work. It grants RAII permits in FIFO order, supports CompletionToken
cancellation, and reports `concurrency_limit_exceeded` when the queue is full
or its wait timeout expires. `try_acquire` is available as a fast path, so an
operation with immediately available capacity does not require an executor
round trip. The limiter never occupies a blocking-executor thread while an
operation waits in the queue.
