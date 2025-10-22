#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/numpy.h>
// removed: #include <pybind11/optional.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <array>
#include <optional>      // added
#include <limits>
#include <stdexcept>
#include <string>

#include "rindle.hpp"
#include "rindle/dataset_types.hpp"
#include "rindle/manifest_types.hpp"
#include "rindle/scaler.hpp"
#include "rindle/types.hpp"


namespace py = pybind11;
namespace rv = rivulet;

namespace {

template <typename T>
T unwrap(rv::Result<T>&& result, const char* function_name) {
    if (!result) {
        std::string message = result.status.message;
        if (message.empty()) {
            message = std::string(function_name) + " failed";
        }
        throw std::runtime_error(message);
    }
    return std::move(result.value.value());
}

py::array tensor_view(rv::Tensor3D& tensor, py::handle base) {
    using value_type = rv::Tensor3D::value_type;

    if (tensor.windows < 0 || tensor.seq_len < 0 || tensor.features < 0) {
        throw std::invalid_argument("Tensor dimensions must be non-negative");
    }

    const auto max_ssize = std::numeric_limits<py::ssize_t>::max();
    if (tensor.windows > max_ssize || tensor.seq_len > max_ssize || tensor.features > max_ssize) {
        throw std::overflow_error("Tensor dimensions exceed Py_ssize_t range");
    }

    const py::ssize_t windows = static_cast<py::ssize_t>(tensor.windows);
    const py::ssize_t seq_len = static_cast<py::ssize_t>(tensor.seq_len);
    const py::ssize_t features = static_cast<py::ssize_t>(tensor.features);
    const py::ssize_t value_size = static_cast<py::ssize_t>(sizeof(value_type));

    auto checked_product = [&](py::ssize_t lhs, py::ssize_t rhs, const char* label) {
        if (lhs != 0 && rhs > max_ssize / lhs) {
            throw std::overflow_error(std::string(label) + " exceeds Py_ssize_t range");
        }
        return lhs * rhs;
    };

    const py::ssize_t stride_feature = value_size;
    const py::ssize_t stride_seq = checked_product(features, stride_feature, "Tensor stride (sequence)");
    const py::ssize_t stride_window = checked_product(seq_len, stride_seq, "Tensor stride (window)");

    std::array<py::ssize_t, 3> shape = {windows, seq_len, features};
    std::array<py::ssize_t, 3> strides = {stride_window, stride_seq, stride_feature};

    return py::array(
        py::dtype::of<value_type>(),
        shape,
        strides,
        tensor.data.data(),
        base
    );
}

} // namespace

PYBIND11_MODULE(rindle, m) {
    m.doc() = "Python bindings for the Rindle dataset preparation library";

    py::enum_<rv::TimeMode>(m, "TimeMode")
        .value("UTC_NS", rv::TimeMode::UTC_NS)
        .value("ORDINAL", rv::TimeMode::ORDINAL)
        .export_values();

    py::enum_<rv::ScalerKind>(m, "ScalerKind")
        .value("None", rv::ScalerKind::None)
        .value("Standard", rv::ScalerKind::Standard)
        .value("ZeroStandard", rv::ScalerKind::ZeroStandard)
        .value("LogStandard", rv::ScalerKind::LogStandard)
        .value("MinMax", rv::ScalerKind::MinMax)
        .value("Robust", rv::ScalerKind::Robust)
        .export_values();

    py::class_<rv::Status>(m, "Status")
        .def(py::init<>())
        .def_readwrite("ok", &rv::Status::ok)
        .def_readwrite("message", &rv::Status::message);

    py::class_<rv::ColumnStats>(m, "ColumnStats")
        .def(py::init<>())
        .def_readwrite("mean", &rv::ColumnStats::mean)
        .def_readwrite("std", &rv::ColumnStats::std)
        .def_readwrite("median", &rv::ColumnStats::median)
        .def_readwrite("iqr", &rv::ColumnStats::iqr)
        .def_readwrite("min", &rv::ColumnStats::min)
        .def_readwrite("max", &rv::ColumnStats::max)
        .def_readwrite("n_samples", &rv::ColumnStats::n_samples);

    py::class_<rv::ScalerParams>(m, "ScalerParams")
        .def(py::init<>())
        .def_readwrite("kind", &rv::ScalerParams::kind)
        .def_readwrite("stats", &rv::ScalerParams::stats)
        .def_readwrite("clip_lo", &rv::ScalerParams::clip_lo)
        .def_readwrite("clip_hi", &rv::ScalerParams::clip_hi);

    py::class_<rv::FittedScaler>(m, "FittedScaler")
        .def(py::init<>())
        .def("transform", &rv::FittedScaler::transform, py::arg("value"))
        .def("inverse_transform", &rv::FittedScaler::inverse_transform, py::arg("value"))
        .def_property_readonly(
            "params",
            [](const rv::FittedScaler& self) {
                return self.params();
            },
            py::return_value_policy::copy
        );

    py::class_<rv::FeatureScalerParams>(m, "FeatureScalerParams")
        .def(py::init<>())
        .def_readwrite("feature", &rv::FeatureScalerParams::feature)
        .def_readwrite("params", &rv::FeatureScalerParams::params);

    py::class_<rv::TickerStats>(m, "TickerStats")
        .def(py::init<>())
        .def_readwrite("ticker", &rv::TickerStats::ticker)
        .def_readwrite("input_rows", &rv::TickerStats::input_rows)
        .def_readwrite("processed_rows", &rv::TickerStats::processed_rows)
        .def_readwrite("windows_created", &rv::TickerStats::windows_created)
        .def_readwrite("was_sorted", &rv::TickerStats::was_sorted)
        .def_readwrite("scaler_kind", &rv::TickerStats::scaler_kind)
        .def_readwrite("feature_scalers", &rv::TickerStats::feature_scalers);

    py::class_<rv::DatasetConfig>(m, "DatasetConfig")
        .def(py::init<>())
        .def_readwrite("input_dir", &rv::DatasetConfig::input_dir)
        .def_readwrite("output_dir", &rv::DatasetConfig::output_dir)
        .def_readwrite("feature_columns", &rv::DatasetConfig::feature_columns)
        .def_readwrite("target_column", &rv::DatasetConfig::target_column)
        .def_readwrite("seq_length", &rv::DatasetConfig::seq_length)
        .def_readwrite("future_horizon", &rv::DatasetConfig::future_horizon)
        .def_readwrite("time_mode", &rv::DatasetConfig::time_mode)
        .def_readwrite("row_major", &rv::DatasetConfig::row_major)
        .def_readwrite("scaler_kind", &rv::DatasetConfig::scaler_kind);

    py::class_<rv::WindowMeta>(m, "WindowMeta")
        .def(py::init<>())
        .def_readwrite("ticker", &rv::WindowMeta::ticker)
        .def_readwrite("start_row", &rv::WindowMeta::start_row)
        .def_readwrite("end_row", &rv::WindowMeta::end_row)
        .def_readwrite("target_start", &rv::WindowMeta::target_start)
        .def_readwrite("target_end", &rv::WindowMeta::target_end);

    py::class_<rv::ManifestContent>(m, "ManifestContent")
        .def(py::init<>())
        .def_readwrite("version", &rv::ManifestContent::version)
        .def_readwrite("seq_length", &rv::ManifestContent::seq_length)
        .def_readwrite("future_horizon", &rv::ManifestContent::future_horizon)
        .def_readwrite("feature_columns", &rv::ManifestContent::feature_columns)
        .def_readwrite("target_column", &rv::ManifestContent::target_column)
        .def_readwrite("time_mode", &rv::ManifestContent::time_mode)
        .def_readwrite("row_major", &rv::ManifestContent::row_major)
        .def_readwrite("scaler_kind", &rv::ManifestContent::scaler_kind)
        .def_readwrite("total_tickers", &rv::ManifestContent::total_tickers)
        .def_readwrite("total_windows", &rv::ManifestContent::total_windows)
        .def_readwrite("total_input_rows", &rv::ManifestContent::total_input_rows)
        .def_readwrite("ticker_stats", &rv::ManifestContent::ticker_stats)
        .def_readwrite("ticker_index", &rv::ManifestContent::ticker_index)
        .def_readwrite("input_dir", &rv::ManifestContent::input_dir)
        .def_readwrite("output_dir", &rv::ManifestContent::output_dir)
        .def_readwrite("build_timestamp", &rv::ManifestContent::build_timestamp)
        .def("build_ticker_index", &rv::ManifestContent::build_ticker_index)
        .def("find_stats", [](const rv::ManifestContent& manifest, std::string_view name) {
            const rv::TickerStats* stats = manifest.find_stats(name);
            if (stats) {
                return py::cast(*stats, py::return_value_policy::copy);
            }
            return py::object(py::none());
        }, py::arg("name"));

    py::class_<rv::Dataset>(m, "Dataset")
        .def(py::init<>())
        .def_property_readonly("X", [](rv::Dataset& self) {
            return tensor_view(self.X, py::cast(&self, py::return_value_policy::reference));
        })
        .def_property_readonly("Y", [](rv::Dataset& self) {
            return tensor_view(self.Y, py::cast(&self, py::return_value_policy::reference));
        })
        .def_readwrite("meta", &rv::Dataset::meta)
        .def("n_windows", &rv::Dataset::n_windows)
        .def("seq_length", &rv::Dataset::seq_length)
        .def("n_features", &rv::Dataset::n_features)
        .def("n_target_features", &rv::Dataset::n_target_features)
        .def("aligned_by_window_and_time", &rv::Dataset::aligned_by_window_and_time);

    m.def(
        "create_config",
        [](const std::filesystem::path& input_dir,
           const std::filesystem::path& output_dir,
           const std::vector<std::string>& feature_columns,
           std::size_t seq_length,
           std::size_t future_horizon,
           const std::optional<std::string>& target_column,
           rv::TimeMode time_mode,
           bool row_major,
           rv::ScalerKind scaler_kind) {
            return unwrap(
                rv::create_config(
                    input_dir,
                    output_dir,
                    feature_columns,
                    seq_length,
                    future_horizon,
                    target_column,
                    time_mode,
                    row_major,
                    scaler_kind),
                "create_config"
            );
        },
        py::arg("input_dir"),
        py::arg("output_dir"),
        py::arg("feature_columns"),
        py::arg("seq_length"),
        py::arg("future_horizon"),
        py::arg("target_column") = std::optional<std::string>{},
        py::arg("time_mode") = rv::TimeMode::UTC_NS,
        py::arg("row_major") = false,
        py::arg("scaler_kind") = rv::ScalerKind::Standard,
        "Create a dataset configuration with validation"
    );

    m.def(
        "build_dataset",
        [](const rv::DatasetConfig& config) {
            return unwrap(rv::build_dataset(config), "build_dataset");
        },
        py::arg("config"),
        "Run the dataset build pipeline and return the manifest"
    );

    m.def(
        "get_dataset",
        [](const rv::ManifestContent& manifest) {
            return unwrap(rv::get_dataset(manifest), "get_dataset");
        },
        py::arg("manifest"),
        "Load dataset tensors from a manifest object"
    );

    m.def(
        "get_dataset",
        [](const std::filesystem::path& manifest_path) {
            return unwrap(rv::get_dataset(manifest_path), "get_dataset");
        },
        py::arg("manifest_path"),
        "Load dataset tensors from a manifest file path"
    );

    m.def(
        "get_feature_scaler",
        [](const rv::ManifestContent& manifest,
           const std::string& ticker,
           const std::string& feature) {
            return unwrap(
                rv::get_feature_scaler(manifest, ticker, feature),
                "get_feature_scaler"
            );
        },
        py::arg("manifest"),
        py::arg("ticker"),
        py::arg("feature"),
        "Fetch a fitted scaler for a ticker/feature pair from an in-memory manifest"
    );

    m.def(
        "get_feature_scaler",
        [](const std::filesystem::path& manifest_path,
           const std::string& ticker,
           const std::string& feature) {
            return unwrap(
                rv::get_feature_scaler(manifest_path, ticker, feature),
                "get_feature_scaler"
            );
        },
        py::arg("manifest_path"),
        py::arg("ticker"),
        py::arg("feature"),
        "Fetch a fitted scaler for a ticker/feature pair by reading manifest.json"
    );

    m.def(
        "inverse_transform_value",
        [](const rv::FittedScaler& scaler, double value) {
            return rv::inverse_transform_value(scaler, value);
        },
        py::arg("scaler"),
        py::arg("value"),
        "Invert a scaled value using the fitted scaler"
    );
}