# Extora benchmarks

These benchmarks measure object reads, writes, metadata operations, listing,
and concurrent I/O through the Extora public API.

## Build and Run

```sh
cmake --preset gcc-release-static
cmake --build --preset gcc-release-static --target ExtoraBenchmarkObjectStore
build/gcc-release-static/bin/ExtoraBenchmarkObjectStore \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true
```

Raw Google Benchmark results are written to
`build/gcc-release-static/benchmarks/ObjectStore/ExtoraBenchmarkObjectStore.json`.
A compact summary is printed after the run.
