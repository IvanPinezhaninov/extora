# Extora examples

These examples demonstrate the Extora public API. Start with `simple` for the
shortest complete object storage flow.

## Build and Run

```sh
cmake -S . -B build/examples -DEXTORA_BUILD_EXAMPLES=ON
cmake --build build/examples --parallel
build/examples/examples/Simple/extora-simple-example
```

## Examples

- `simple` stores and reads an object.
- `object_lifecycle` demonstrates the object and bucket lifecycle.
- `custom_checksum` adds a custom checksum algorithm.
- `observability` reports operation lifecycle and progress events.
- `multipart` performs a multipart upload.
- `versioning` demonstrates object versions and delete markers.
- [`http_server`](http_server/README.md) provides a small HTTP server for local
  testing.
