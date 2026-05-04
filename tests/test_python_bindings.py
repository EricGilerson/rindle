import os
import shutil
import tempfile

import pytest

DATA_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "raw")
FEATURES = ["Price_0939", "Prev_Delta_Close", "Gap", "Composite_HL"]
TARGET = "Delta_Close"


@pytest.fixture()
def output_dir():
    d = tempfile.mkdtemp(prefix="rindle_pytest_")
    yield d
    shutil.rmtree(d, ignore_errors=True)


# ── Enums ─────────────────────────────────────────────────────────────────


class TestEnums:
    def test_time_mode_values(self):
        import rindle

        assert rindle.TimeMode.UTC_NS is not None
        assert rindle.TimeMode.ORDINAL is not None

    def test_scaler_kind_values(self):
        import rindle

        for kind in [
            rindle.ScalerKind.Standard,
            rindle.ScalerKind.MinMax,
            rindle.ScalerKind.Robust,
            rindle.ScalerKind.ZeroStandard,
            rindle.ScalerKind.LogStandard,
        ]:
            assert kind is not None


# ── create_config ─────────────────────────────────────────────────────────


class TestCreateConfig:
    def test_valid_config(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        assert config.seq_length == 50
        assert config.future_horizon == 1
        assert config.feature_columns == FEATURES

    def test_invalid_dir_raises(self, output_dir):
        import rindle

        with pytest.raises(RuntimeError, match="does not exist"):
            rindle.create_config("/nonexistent_dir", output_dir, FEATURES, 50, 1)

    def test_empty_features_raises(self, output_dir):
        import rindle

        with pytest.raises(RuntimeError):
            rindle.create_config(DATA_DIR, output_dir, [], 50, 1)


# ── Full pipeline ─────────────────────────────────────────────────────────


class TestFullPipeline:
    def test_build_and_load(self, output_dir):
        import numpy as np

        import rindle

        config = rindle.create_config(
            DATA_DIR,
            output_dir,
            FEATURES,
            50,
            1,
            TARGET,
            rindle.TimeMode.UTC_NS,
            False,
            rindle.ScalerKind.Standard,
        )
        manifest = rindle.build_dataset(config, thread_count=1)

        assert manifest.total_tickers == 3
        assert manifest.total_windows > 0

        dataset = rindle.get_dataset(manifest, percentage=1.0, thread_count=1)
        assert dataset.n_windows() == manifest.total_windows

        X = dataset.X
        Y = dataset.Y
        assert isinstance(X, np.ndarray)
        assert X.ndim == 3
        assert X.shape == (manifest.total_windows, 50, len(FEATURES))
        assert Y.ndim == 3
        assert Y.shape[0] == manifest.total_windows

    def test_subsampling(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        manifest = rindle.build_dataset(config, thread_count=1)
        dataset = rindle.get_dataset(manifest, percentage=0.5, thread_count=1)
        assert dataset.n_windows() < manifest.total_windows
        assert dataset.n_windows() > 0


# ── Scaler ────────────────────────────────────────────────────────────────


class TestGetFeatureScaler:
    def test_scaler_roundtrip(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        manifest = rindle.build_dataset(config, thread_count=1)
        scaler = rindle.get_feature_scaler(manifest, "ABNB", "Price_0939")

        raw = 150.0
        scaled = scaler.transform(raw)
        restored = rindle.inverse_transform_value(scaler, scaled)
        assert abs(restored - raw) < 1e-6

    def test_missing_ticker_raises(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        manifest = rindle.build_dataset(config, thread_count=1)
        with pytest.raises(RuntimeError, match="not found"):
            rindle.get_feature_scaler(manifest, "ZZZZ", "Price_0939")

    def test_from_path(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        rindle.build_dataset(config, thread_count=1)
        manifest_path = os.path.join(output_dir, "manifest.json")
        scaler = rindle.get_feature_scaler(manifest_path, "ABNB", "Price_0939")
        assert scaler.params.kind == rindle.ScalerKind.Standard


# ── Dataset properties ────────────────────────────────────────────────────


class TestDatasetProperties:
    def test_numpy_array_properties(self, output_dir):
        import numpy as np

        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        manifest = rindle.build_dataset(config, thread_count=1)
        dataset = rindle.get_dataset(manifest, percentage=1.0, thread_count=1)

        X = dataset.X
        assert X.dtype == np.float32
        assert X.flags["C_CONTIGUOUS"]

    def test_meta_access(self, output_dir):
        import rindle

        config = rindle.create_config(DATA_DIR, output_dir, FEATURES, 50, 1, TARGET)
        manifest = rindle.build_dataset(config, thread_count=1)
        dataset = rindle.get_dataset(manifest, percentage=1.0, thread_count=1)

        assert len(dataset.meta) == manifest.total_windows
        meta = dataset.meta[0]
        assert hasattr(meta, "ticker")
        assert hasattr(meta, "start_row")
        assert hasattr(meta, "end_row")
        assert meta.ticker in ("ABNB", "ABBV", "ABT")
