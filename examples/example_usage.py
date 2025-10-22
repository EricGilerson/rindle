from __future__ import annotations

from pathlib import Path

import numpy as np

import rindle


def print_last_x_sequence(dataset: rindle.Dataset) -> None:
    """Pretty-print the final feature window from the dataset."""
    X = np.asarray(dataset.X)
    if X.shape[0] == 0:
        print("\nNo X windows available to print.")
        return

    widx = X.shape[0] - 1
    print("\n============================================================")
    print(
        f"Last X sequence (window index {widx}) with shape "
        f"[{X.shape[1]}, {X.shape[2]}]"
    )
    print("Each row below is one timestep; values are comma-separated features.")
    print("============================================================")

    for t in range(X.shape[1]):
        row = ", ".join(f"{X[widx, t, f]:.6f}" for f in range(X.shape[2]))
        print(f"t={t}: {row}")


def main() -> None:
    """Run the standard Rindle workflow using the Python bindings."""
    print("Creating configuration...")
    config = rindle.create_config(
        Path("data/raw"),
        Path("data/processed"),
        ["Price_0939", "Prev_Delta_Close", "Gap", "Composite_HL"],
        seq_length=50,
        future_horizon=1,
        target_column="Delta_Close",
        time_mode=rindle.TimeMode.UTC_NS,
        row_major=False,
        scaler_kind=rindle.ScalerKind.Standard,
    )
    print("Configuration created successfully!")

    print("\nBuilding dataset...")
    manifest = rindle.build_dataset(config)
    print("Dataset built successfully!")
    print(f"  Total tickers: {manifest.total_tickers}")
    print(f"  Total windows: {manifest.total_windows}")
    print(f"  Total input rows: {manifest.total_input_rows}")
    print(f"  Feature scaler: {manifest.scaler_kind.name}")

    print("\nLoading dataset tensors...")
    dataset = rindle.get_dataset(manifest)
    print("Dataset loaded successfully!")

    X = np.asarray(dataset.X)
    Y = np.asarray(dataset.Y)
    print(f"  X shape: [{X.shape[0]}, {X.shape[1]}, {X.shape[2]}]")
    print(f"  Y shape: [{Y.shape[0]}, {Y.shape[1]}, {Y.shape[2]}]")

    if X.size:
        first_value = float(X[0, 0, 0])
        print(f"  First X value: {first_value}")
    if Y.size:
        first_target = float(Y[0, 0, 0])
        print(f"  First Y value: {first_target}")

    if dataset.meta and manifest.feature_columns:
        first_ticker = dataset.meta[0].ticker
        first_feature = manifest.feature_columns[0]
        scaler = rindle.get_feature_scaler(manifest, first_ticker, first_feature)
        original = rindle.inverse_transform_value(scaler, float(X[0, 0, 0]))
        print(f"  Inverse-transformed first X value: {original}")

    print_last_x_sequence(dataset)


if __name__ == "__main__":
    main()