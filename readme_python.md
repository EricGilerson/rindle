# Rindle for Python

Rindle turns collections of per-ticker CSV files into contiguous sliding-window
tensors that are ready for deep learning workflows. The Python extension wraps
the C++20 data preparation engine behind a small, NumPy-friendly API so you can
configure builds, materialize datasets, and recover fitted scalers directly
from notebooks or training scripts.

## Highlights

- **Deterministic dataset builds** – declare the window geometry, scaler, and
  input schema with `rindle.create_config` and let the engine emit consistent
  results across runs.
- **Manifest-driven reloads** – rehydrate tensors on demand with
  `rindle.get_dataset` using the in-memory manifest returned by a build or a
  saved `manifest.json` file.
- **NumPy integration** – feature (`Dataset.X`) and target (`Dataset.Y`) tensors
  are exposed as NumPy arrays with shape `(windows, sequence_length, features)`
  and `float32` precision for direct use with frameworks such as PyTorch or
  TensorFlow.
- **Scaler introspection** – fetch the fitted scaler for any ticker/feature pair
  to invert predictions or understand the normalization that was applied.

## Installation

The package ships with pre-built wheels when possible and can also be compiled
locally with a C++20 toolchain.

```bash
pip install rindle
```

Building from source requires a compiler with C++20 support, CMake 3.18+, and
Python 3.9 or newer. When working from a clone of the repository:

```bash
python -m pip install --upgrade pip
python -m pip install build
python -m build
python -m pip install dist/rindle-*.whl
```

## Quickstart

```python
from pathlib import Path
import rindle

config = rindle.create_config(
    input_dir=Path("data/raw_prices"),
    output_dir=Path("data/processed"),
    feature_columns=["Open", "High", "Low", "Close", "Volume"],
    seq_length=64,
    future_horizon=8,
    target_column="Close",
    time_mode=rindle.TimeMode.UTC_NS,
    row_major=False,
    scaler_kind=rindle.ScalerKind.Standard,
)

manifest = rindle.build_dataset(config)
dataset = rindle.get_dataset(manifest)

X = dataset.X  # NumPy array: (windows, seq_length, n_features), dtype=float32
Y = dataset.Y  # NumPy array aligned with X when targets are enabled
meta = dataset.meta  # List of WindowMeta objects with ticker provenance
print("total windows:", dataset.n_windows())
```

The manifest stores the configuration, aggregate statistics, and ticker-level
metadata. A copy is written to `<output_dir>/manifest.json` during the build so
you can reload tensors later without repeating the pipeline:

```python
from pathlib import Path

manifest_path = Path(config.output_dir) / "manifest.json"
reloaded = rindle.get_dataset(manifest_path)
```

## Inspecting manifests and scalers

Each `ManifestContent` instance exposes the fields captured during the build,
including `feature_columns`, `total_windows`, and `ticker_stats`. The helper
method `find_stats("AAPL")` returns the `TickerStats` record for a ticker, and
`build_ticker_index()` can be called if you mutate `ticker_stats` manually.

To invert normalized values or apply identical scaling elsewhere:

```python
scaler = rindle.get_feature_scaler(manifest, ticker="AAPL", feature="Close")
original_value = rindle.inverse_transform_value(scaler, value=0.42)
```

The returned `FittedScaler` exposes `transform` and `inverse_transform` methods
as well as a `params` property that includes summary statistics (mean, standard
deviation, quartiles, and min/max bounds).

## Data layout

- `Dataset.X` and `Dataset.Y` are three-dimensional NumPy arrays backed by the
  underlying C++ tensors (`float32`). When `row_major=False` (the default), the
  layout is `[window][time][feature]` with contiguous storage, making it ideal
  for training recurrent and convolutional models.
- `Dataset.meta` is a list of `WindowMeta` objects describing where each window
  originated. Fields include `ticker`, `start_row`, `end_row`, and optional
  `target_start` / `target_end` indices.

## API reference snapshot

| Function | Description |
| --- | --- |
| `rindle.create_config(...)` | Validate paths, choose feature columns, configure window geometry and scaling. Returns a `DatasetConfig`. |
| `rindle.build_dataset(config)` | Run discovery → scaling → windowing and return a `ManifestContent`. |
| `rindle.get_dataset(manifest_or_path)` | Load feature/target tensors from an in-memory manifest or a saved `manifest.json`. |
| `rindle.get_feature_scaler(manifest_or_path, ticker, feature)` | Retrieve the fitted scaler for a ticker/feature pair to apply or invert scaling. |
| `rindle.inverse_transform_value(scaler, value)` | Convenience helper to undo scaling with a `FittedScaler`. |

Additional classes such as `DatasetConfig`, `ManifestContent`, `Dataset`, and
`TickerStats` expose their fields as Python attributes for straightforward
inspection or serialization.

## Project resources

- Source repository: <https://github.com/EricGilerson/rindle>
- Issue tracker: <https://github.com/EricGilerson/rindle/issues>

Although the core engine is implemented in C++, the Python package provides a
self-contained workflow for assembling time-series datasets without leaving the
Python ecosystem.
