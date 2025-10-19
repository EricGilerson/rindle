//
// Created by Eric Gilerson on 10/7/25.
//

#ifndef RIVULET_SCALER_HPP
#define RIVULET_SCALER_HPP

#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>

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
        // optional clip bounds
        std::optional<double> clip_lo;
        std::optional<double> clip_hi;
    };

    class Scaler {
    public:
        virtual ~Scaler() = default;

        // Compute statistics from training data
        virtual void fit(const std::vector<double>& col) = 0;

        // Apply transformation
        virtual void transform(std::vector<double>& col) const = 0;

        // Get fitted parameters
        virtual ScalerParams params() const = 0;

        // Apply inverse transformation
        virtual void inverse_transform(std::vector<double>& col) const = 0;
    };

    // Factory function
    std::unique_ptr<Scaler> make_scaler(
        ScalerKind kind,
        std::optional<std::pair<double,double>> clip = std::nullopt
    );

    // Feature specification with scaling
    struct FeatureSpec {
        std::string name;       // e.g., "RSI_14"
        std::string column;     // source column in the table
        ScalerKind scaler = ScalerKind::None;
        std::optional<std::pair<double,double>> clip;
    };

    // Store fitted scalers for all columns
    struct ScalerStore {
        // per-column fitted params
        std::unordered_map<std::string, ScalerParams> by_column;

        // Serialize to JSON
        std::string to_json() const;

        // Deserialize from JSON
        static std::optional<ScalerStore> from_json(const std::string& json);
    };

} // namespace rivulet

#endif //RIVULET_SCALER_HPP
