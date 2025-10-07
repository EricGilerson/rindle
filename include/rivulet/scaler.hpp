//
// Created by Eric Gilerson on 10/7/25.
//

#ifndef RIVULET_SCALER_HPP
#define RIVULET_SCALER_HPP

#endif //RIVULET_SCALER_HPP

#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace rivulet {

    enum class ScalerKind { None, Standard, ZeroStandard, LogStandard, MinMax, Robust };

    struct ColumnStats {
        double mean = 0.0;
        double std = 1.0;
        double median = 0.0;
        double iqr = 1.0;
        double min = 0.0;
        double max = 0.0;
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
        virtual void fit(const std::vector<double>& col) = 0;
        virtual void transform(std::vector<double>& col) const = 0;
        virtual ScalerParams params() const = 0;
    };

    std::unique_ptr<Scaler> make_scaler(ScalerKind kind, std::optional<std::pair<double,double>> clip = std::nullopt);

    struct FeatureSpec {
        std::string name;       // e.g., "RSI_14"
        std::string column;     // source column in the table
        ScalerKind scaler = ScalerKind::None;
    };

    struct ScalerStore {
        // per-column fitted params
        std::unordered_map<std::string, ScalerParams> by_column;
    };

} // namespace rivulet
