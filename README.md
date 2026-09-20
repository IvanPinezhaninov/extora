# Extora

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17.html)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/IvanPinezhaninov/extora/ci.yml)](https://github.com/IvanPinezhaninov/extora/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fivanpinezhaninov.github.io%2Fextora%2Fcoverage.json)](https://ivanpinezhaninov.github.io/extora/)

Extora is a C++17 library for embedded object storage. It provides streamed
bucket and object operations, metadata, listing, range reads, checksums,
versioning, and multipart uploads. Object data is stored in preallocated
append-oriented segment files.

The optional Boost.Asio adapter provides CompletionToken-based asynchronous
storage operations.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Extora requires CMake 3.28 or newer and a C++17 compiler. Top-level builds use
pinned SQLite and xxHash archives by default. GoogleTest is only needed for
tests, and Boost is only needed for the optional Asio adapter and HTTP server
example. If a parent project already provides compatible dependency targets,
Extora reuses them. Set the corresponding `EXTORA_USE_BUNDLED_*` option to
`OFF` to use installed packages instead.

## Usage

```cpp
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include <extora/extora.h>

class string_reader final : public extora::object_reader {
public:
  explicit string_reader(std::string_view value)
    : m_value{value}
  {}

  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    const std::size_t count = std::min(size, m_value.size() - m_offset);
    std::memcpy(data, m_value.data() + m_offset, count);
    m_offset += count;
    return {count, m_offset == m_value.size(), {}};
  }

private:
  std::string_view m_value;
  std::size_t m_offset = 0;
};

int main()
{
  extora::object_store_options options;
  options.root_directory = "extora-store";
  extora::storage_error error;
  auto store = extora::open_object_store(options, error);
  if (!store)
    return 1;

  const extora::bucket_name bucket{"example"};
  const extora::object_key key{"hello.txt"};

  error = store->create_bucket(bucket);
  if (extora::failed(error) && error.code != extora::storage_error_code::bucket_already_exists)
    return 1;

  constexpr std::string_view content = "Hello, Extora!";
  string_reader reader{content};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";

  extora::put_object_result put_result;
  error = store->put_object(bucket, key, reader, metadata, put_result);
  if (extora::failed(error))
    return 1;

  extora::open_object_result open_result;
  error = store->open_object(bucket, key, open_result);
  if (extora::failed(error))
    return 1;

  std::string downloaded;
  downloaded.reserve(open_result.object.content_length);
  std::array<std::byte, 4096> buffer;
  while (true) {
    const extora::object_read_result chunk = open_result.reader->read(buffer.data(), buffer.size());
    downloaded.append(reinterpret_cast<const char*>(buffer.data()), chunk.bytes_read);
    if (extora::failed(chunk.error))
      return 1;
    if (chunk.end_of_stream)
      break;
    if (chunk.bytes_read == 0)
      return 1;
  }

  std::cout << downloaded << '\n';
  return downloaded == content ? 0 : 1;
}
```

More complete programs live in [examples](examples/).

Consume an installed package with:

```cmake
find_package(extora CONFIG REQUIRED)
target_link_libraries(MyApplication PRIVATE extora::extora)
```

When Extora is added with `FetchContent` or `add_subdirectory`, omit
`find_package` and link the same target. Applications using the optional
Boost.Asio adapter link with `extora::asio`.

## License

Extora is distributed under the [MIT License](LICENSE).

## Author

[Ivan Pinezhaninov](mailto:ivan.pinezhaninov@gmail.com)
