# Rindle

Rindle is a C++20 library for turning raw, per-ticker CSV files into training-ready
datasets. It discovers input files, learns feature scalers, generates sliding
windows, and produces both a manifest and contiguous tensors that can be consumed
from C++ or Python.

## High-level workflow

1. **Configure** – call `rindle::create_config` to validate paths, select feature
   columns, and choose window geometry and scaling options.【F:include/rindle.hpp†L23-L59】
2. **Build** – pass the configuration to `rindle::build_dataset`; the driver
   discovers tickers, fits scalers, writes window manifests, and emits
   `manifest.json` summarizing the build.【F:src/driver.cpp†L19-L125】【F:include/rindle.hpp†L61-L67】
3. **Load** – use `rindle::get_dataset` with the manifest to materialize feature
   and target tensors in memory for model training or analysis.【F:include/rindle.hpp†L69-L90】【F:src/rindle_api.cpp†L95-L169】

The C++ API is mirrored in the optional Python bindings, enabling the same flow
from notebooks or scripts.【F:src/python/bindings.cpp†L97-L214】

## Input expectations

* Each ticker lives in its own CSV file inside the configured input directory.
* Files must include a header whose first column is `Date`; the remaining columns
  are treated as numeric features. Missing numeric values are parsed as
  `NaN` and timestamps may be provided as ISO-8601 strings or integer epochs in
  seconds through nanoseconds.【F:src/internal/csv_io.hpp†L30-L65】【F:src/csv_io.cpp†L34-L121】
* Ticker symbols are derived from filenames (sans extension) and normalized to
  uppercase without whitespace.【F:src/catalog.cpp†L43-L70】

## Generated artifacts

Running `build_dataset` creates the following outputs:

* **Per-ticker window manifests** – each ticker produces a binary manifest file
  (currently named `*_windows.parquet`) that records every window's index range
  and optional target span.【F:src/driver.cpp†L139-L157】【F:src/internal/window_manifest.hpp†L23-L65】
* **`manifest.json`** – captures dataset-level metadata such as feature lists,
  scaler choices, window counts, and per-ticker statistics. It also stores the
  build timestamp, input/output directories, and a lookup table for ticker
  statistics.【F:include/rindle/manifest_types.hpp†L16-L52】【F:src/manifest.cpp†L23-L109】

The manifest content can be reused later to reload tensors without repeating the
entire pipeline.【F:src/rindle_api.cpp†L133-L169】

## Tensor layout

Datasets are represented by lightweight tensor wrappers that store contiguous
feature (`X`) and target (`Y`) data along with window metadata:

* `Tensor3D` models a `[window, sequence, feature]` cube in row-major order and
  exposes helpers for indexing within a flat buffer.【F:include/rindle/dataset_types.hpp†L17-L49】
* `Dataset` holds the feature/target tensors plus a `WindowMeta` vector that
  tracks the source ticker and row ranges for every window.【F:include/rindle/dataset_types.hpp†L51-L80】

## Scaler support

Rindle offers several built-in scaling strategies and records the fitted
parameters alongside the manifest:

* `ScalerKind` enumerates available scalers (standard, min-max, robust, etc.) and
  is stored in the dataset configuration and manifest.【F:include/rindle/scaler.hpp†L17-L63】【F:include/rindle/manifest_types.hpp†L19-L32】
* `ScalerStore` serializes the per-feature statistics to JSON for reuse, and CSV
  helpers exist to persist or reload artifact bundles if needed.【F:include/rindle/scaler.hpp†L65-L105】【F:src/csv_io.cpp†L151-L236】

## Window generation

Sliding windows are produced using ticker-level statistics exposed by the
manifest. The window maker can stream results to a sink (for writing manifests)
or return them as in-memory vectors for smaller workloads.【F:src/window_maker.cpp†L1-L149】

## Directory structure

```
include/        # Public headers (API, types, scalers)
src/            # Library implementation and internal headers
src/python/     # pybind11 bindings for the public API
examples/       # End-to-end usage demonstrations (C++ and Python)
data/           # Sample raw/processed directories for experimentation
tests/          # Catch2 test harness (placeholder)
```

## Building the library

```bash
cmake -S . -B build \
      -DRINDLE_BUILD_TESTS=ON \
      -DRINDLE_BUILD_EXAMPLES=ON \
      -DRINDLE_BUILD_PYTHON=ON
cmake --build build
```

The project targets C++20, fetches `nlohmann_json`, and optionally brings in
Catch2 and pybind11 for tests and bindings.【F:CMakeLists.txt†L1-L102】 Use
`cmake --build build --target rindle_tests` followed by `ctest --test-dir build`
to run the test suite when implemented.【F:CMakeLists.txt†L69-L87】

## Python bindings

Enable `RINDLE_BUILD_PYTHON` to build the `rindle` Python module. The bindings
expose tensor views as NumPy arrays while reusing the same configuration and
loading APIs as C++.【F:src/python/bindings.cpp†L19-L214】 The generated extension
module is placed in the build tree (e.g., `build/src/python/rindle.*`).

## Distributing on PyPI or installing via pip

The repository ships a `pyproject.toml` configured with
[`scikit-build-core`](https://scikit-build-core.readthedocs.io/) so the C++
extension can be packaged like a standard Python project.【F:pyproject.toml†L1-L40】
The Python package re-exports the compiled module, exposes a version sourced
from package metadata, and keeps the import path as `import rindle` for existing
scripts.【F:python/rindle/__init__.py†L1-L23】

Build and install a wheel locally with pip:

```bash
pip install .
```

For distribution, create a wheel and publish it to an index:

```bash
python -m build
twine upload dist/*
```

Both commands honour the CMake options in the project file; tests and examples
are disabled automatically during wheel builds to keep artifacts minimal while
still compiling the Python bindings.【F:pyproject.toml†L42-L45】

## Examples

The `examples` directory contains runnable demonstrations for both languages:

* `examples/example_usage.cpp` walks through the full C++ workflow, from
  configuration to printing summary statistics and inspecting windows.
* `examples/example_usage.py` mirrors the process using the Python bindings and
  NumPy for inspection.

Build the C++ example with the `RINDLE_BUILD_EXAMPLES` option and run the Python
script after building the bindings.

## Next steps

The Catch2 harness in `tests/` is ready for assertions once real scenarios are
added, and the window manifest writer currently produces a lightweight binary
format that can later be swapped for an actual Parquet implementation without
changing the public API.【F:src/internal/window_manifest.hpp†L23-L65】