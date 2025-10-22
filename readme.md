# Rindle

Rindle is a C++20 library for turning raw, per-ticker CSV files into training-ready
datasets. It discovers input files, learns feature scalers, generates sliding
windows, and produces both a manifest and contiguous tensors that can be consumed
from C++ or Python.

## High-level workflow

1. **Configure** – call `rindle::create_config` to validate paths, select feature
   columns, and choose window geometry and scaling options.
2. **Build** – pass the configuration to `rindle::build_dataset`; the driver
   discovers tickers, fits scalers, writes window manifests, and emits
   `manifest.json` summarizing the build.
3. **Load** – use `rindle::get_dataset` with the manifest to materialize feature
   and target tensors in memory for model training or analysis.

The C++ API is mirrored in the optional Python bindings, enabling the same flow
from notebooks or scripts.

## Input expectations

* Each ticker lives in its own CSV file inside the configured input directory.
* Files must include a header whose first column is `Date`; the remaining columns
  are treated as numeric features. Missing numeric values are parsed as
  `NaN` and timestamps may be provided as ISO-8601 strings or integer epochs in
  seconds through nanoseconds.
* Ticker symbols are derived from filenames (sans extension) and normalized to
  uppercase without whitespace.

## Generated artifacts

Running `build_dataset` creates the following outputs:

* **Per-ticker window manifests** – each ticker produces a binary manifest file
  (currently named `*_windows.parquet`) that records every window's index range
  and optional target span.
* **`manifest.json`** – captures dataset-level metadata such as feature lists,
  scaler choices, window counts, and per-ticker statistics. It also stores the
  build timestamp, input/output directories, and a lookup table for ticker
  statistics.

The manifest content can be reused later to reload tensors without repeating the
entire pipeline.

## Tensor layout

Datasets are represented by lightweight tensor wrappers that store contiguous
feature (`X`) and target (`Y`) data along with window metadata:

* `Tensor3D` models a `[window, sequence, feature]` cube in row-major order and
  exposes helpers for indexing within a flat buffer.
* `Dataset` holds the feature/target tensors plus a `WindowMeta` vector that
  tracks the source ticker and row ranges for every window.

## Scaler support

Rindle offers several built-in scaling strategies and records the fitted
parameters alongside the manifest:

* `ScalerKind` enumerates available scalers (standard, min-max, robust, etc.) and
  is stored in the dataset configuration and manifest.
* `ScalerStore` serializes the per-feature statistics to JSON for reuse, and CSV
  helpers exist to persist or reload artifact bundles if needed.
* `get_feature_scaler` returns a `FittedScaler` for a ticker/feature pair using
  either an in-memory manifest or a saved `manifest.json`. The scaler exposes
  `transform`/`inverse_transform` helpers, and the convenience function
  `inverse_transform_value` can recover the original numeric value from the
  scaled tensors returned by `get_dataset`.

## Window generation

Sliding windows are produced using ticker-level statistics exposed by the
manifest. The window maker can stream results to a sink (for writing manifests)
or return them as in-memory vectors for smaller workloads.

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
Catch2 and pybind11 for tests and bindings. Use
`cmake --build build --target rindle_tests` followed by `ctest --test-dir build`
to run the test suite when implemented.

## Python bindings

Enable `RINDLE_BUILD_PYTHON` to build the `rindle` Python module. The bindings
expose tensor views as NumPy arrays while reusing the same configuration and
loading APIs as C++. The generated extension
module is placed in the build tree (e.g., `build/src/python/rindle.*`).

## Distributing on PyPI or installing via pip

The repository ships a `pyproject.toml` configured with
[`scikit-build-core`](https://scikit-build-core.readthedocs.io/) so the C++
extension can be packaged like a standard Python project.
The Python package re-exports the compiled module, exposes a version sourced
from package metadata, and keeps the import path as `import rindle` for existing
scripts.

### Install into a local Python environment

1. **Create (optional) and activate a virtual environment** to keep dependencies
   isolated. Any virtual environment manager works; for the built-in `venv`
   module:

   ```bash
   python -m venv .venv
   source .venv/bin/activate  # On Windows use `.venv\Scripts\activate`
   ```

2. **Install build prerequisites** if you have not already. `pip` can compile
   the extension as long as CMake and a C++20 compiler are available on your
   `PATH`. Installing `build` and `wheel` provides helpful tooling:

   ```bash
   pip install --upgrade pip
   pip install build wheel
   ```

3. **Install Rindle into the environment**. From the repository root run:

   ```bash
   pip install .
   ```

   This command builds the extension with scikit-build-core, installs the
   resulting wheel into the active environment, and exposes `import rindle`.

4. **(Optional) Editable install for iterative development.** If you intend to
   iterate on the bindings, install in editable mode so Python resolves the
   module from your working tree while still compiling the extension as needed:

   ```bash
   pip install --editable .
   ```

5. **Verify the install** by importing the package and checking the version:

   ```bash
   python -c "import rindle; print(rindle.__version__)"
   ```
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
changing the public API.