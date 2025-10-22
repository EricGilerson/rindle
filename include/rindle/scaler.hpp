#ifndef RIVULET_SCALER_PUBLIC_HPP
#define RIVULET_SCALER_PUBLIC_HPP

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace rivulet {

    enum class ScalerKind {
        None,
        Standard,       // (x - mean) / std
        ZeroStandard,   // x / std (for zero-mean data)
        LogStandard,    // log(x+1) then standard scale
        MinMax,         // (x - min) / (max - min)
        Robust          // (x - median) / IQR
    };

    struct ColumnStats {
        double mean = 0.0;
        double std = 1.0;
        double median = 0.0;
        double iqr = 1.0;
        double min = 0.0;
        double max = 0.0;
        std::size_t n_samples = 0;
    };

    struct ScalerParams {
        ScalerKind kind = ScalerKind::None;
        ColumnStats stats;
        std::optional<double> clip_lo;
        std::optional<double> clip_hi;
    };

    class FittedScaler {
    public:
        FittedScaler() = default;
        explicit FittedScaler(ScalerParams params);

        double transform(double value) const;
        double inverse_transform(double value) const;
        const ScalerParams& params() const;

    private:
        ScalerParams params_{};
    };

    class Scaler {
    public:
        virtual ~Scaler() = default;

        virtual void fit(const std::vector<double>& col) = 0;

        virtual void transform(std::vector<double>& col) const = 0;

        virtual ScalerParams params() const = 0;

        virtual void inverse_transform(std::vector<double>& col) const = 0;

        virtual void fit_transform(std::vector<double>& col);
    };

    std::unique_ptr<Scaler> make_scaler(
        ScalerKind kind,
        std::optional<std::pair<double, double>> clip = std::nullopt
    );

    struct FeatureSpec {
        std::string name;
        std::string column;
        ScalerKind scaler = ScalerKind::None;
        std::optional<std::pair<double, double>> clip;
    };

    struct ScalerStore {
        std::unordered_map<std::string, ScalerParams> by_column;

        std::string to_json() const;

        static std::optional<ScalerStore> from_json(const std::string& json);
    };

    std::string scaler_kind_to_string(ScalerKind kind);

    std::optional<ScalerKind> scaler_kind_from_string(std::string_view name);

    nlohmann::json column_stats_to_json(const ColumnStats& stats);

    ColumnStats column_stats_from_json(const nlohmann::json& json);

    nlohmann::json scaler_params_to_json(const ScalerParams& params);

    ScalerParams scaler_params_from_json(const nlohmann::json& json);

    double apply_scaler_value(double value, const ScalerParams& params);

    double inverse_apply_scaler_value(double value, const ScalerParams& params);

    double inverse_transform_value(const FittedScaler& scaler, double value);

} // namespace rivulet

#endif // RIVULET_SCALER_PUBLIC_HPP
