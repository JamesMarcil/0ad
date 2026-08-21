# Google Benchmark (v1.9.5)

Google Benchmark is a microbenchmark support library for C++ code.

* **Upstream Repository**: https://github.com/google/benchmark
* **Version**: v1.9.5 (Tag `v1.9.5`)
* **License**: Apache License 2.0 (see `LICENSE`)

## Vendored Directory Structure
* `include/benchmark/` - Public C++ API headers (`benchmark.h`, `export.h`)
* `src/` - Implementation sources and internal headers

## Integration in 0 A.D.
Configured via `build/premake/extern_libs5.lua` (`add_third_party_include_paths("benchmark")`, `BENCHMARK_STATIC_DEFINE`) and built as a third-party static library linking into the standalone `benchmark` executable target.
