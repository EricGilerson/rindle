#include "rindle/scaler.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace rivulet {
namespace {

constexpr double kEpsilon = 1e-12;

std::vector<double> collect_finite(const std::vector<double>& col) {
    std::vector<double> finite;
    finite.reserve(col.size());
    for (double value : col) {
        if (std::isfinite(value)) {
            finite.push_back(value);
        }
    }
    return finite;
}

double percentile_from_sorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    if (p <= 0.0) {
        return sorted.front();
    }
    if (p >= 1.0) {
        return sorted.back();
    }
    const double pos = p * static_cast<double>(sorted.size() - 1);
    const std::size_t idx = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(idx);
    const double lower = sorted[idx];
    const double upper = sorted[std::min(idx + 1, sorted.size() - 1)];
    return lower + frac * (upper - lower);
}

ColumnStats compute_stats(const std::vector<double>& col) {
    ColumnStats stats{};
    auto finite = collect_finite(col);
    stats.n_samples = finite.size();
    if (finite.empty()) {
        stats.std = 1.0;
        stats.iqr = 1.0;
        return stats;
    }

    auto [min_it, max_it] = std::minmax_element(finite.begin(), finite.end());
    stats.min = *min_it;
    stats.max = *max_it;

    const double sum = std::accumulate(finite.begin(), finite.end(), 0.0);
    stats.mean = sum / static_cast<double>(finite.size());

    double variance = 0.0;
    for (double value : finite) {
        const double diff = value - stats.mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(finite.size());
    if (variance < 0.0) {
        variance = 0.0;
    }
    stats.std = std::sqrt(variance);
    if (std::abs(stats.std) < kEpsilon) {
        stats.std = 1.0;
    }

    std::sort(finite.begin(), finite.end());
    stats.median = percentile_from_sorted(finite, 0.5);
    const double q1 = percentile_from_sorted(finite, 0.25);
    const double q3 = percentile_from_sorted(finite, 0.75);
    stats.iqr = q3 - q1;
    if (std::abs(stats.iqr) < kEpsilon) {
        stats.iqr = 1.0;
    }

    return stats;
}

class BaseScaler : public Scaler {
public:
    explicit BaseScaler(ScalerKind kind, std::optional<std::pair<double, double>> clip)
        : clip_(std::move(clip)), kind_(kind) {}

    ScalerParams params() const override {
        ScalerParams params;
        params.kind = kind_;
        params.stats = stats_;
        if (clip_) {
            params.clip_lo = clip_->first;
            params.clip_hi = clip_->second;
        }
        return params;
    }

protected:
    void set_stats(const ColumnStats& stats) { stats_ = stats; }

    double apply_clip(double value) const {
        if (clip_) {
            if (clip_->first > clip_->second) {
                return value;
            }
            value = std::max(clip_->first, value);
            value = std::min(clip_->second, value);
        }
        return value;
    }

    double safe_denominator(double value) const {
        return (std::abs(value) < kEpsilon) ? 1.0 : value;
    }

    ColumnStats stats_{};
    std::optional<std::pair<double, double>> clip_;
    ScalerKind kind_;
};

class IdentityScaler final : public BaseScaler {
public:
    explicit IdentityScaler(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::None, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        set_stats(compute_stats(col));
    }

    void transform(std::vector<double>& col) const override {
        if (!clip_) {
            return;
        }
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            value = apply_clip(value);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        // Identity transform is symmetric; nothing to do.
        (void)col;
    }
};

class StandardScalerImpl final : public BaseScaler {
public:
    explicit StandardScalerImpl(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::Standard, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        set_stats(compute_stats(col));
    }

    void transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            const double scaled = (value - stats_.mean) / denom;
            value = apply_clip(scaled);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            value = (value * denom) + stats_.mean;
        }
    }
};

class ZeroStandardScalerImpl final : public BaseScaler {
public:
    explicit ZeroStandardScalerImpl(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::ZeroStandard, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        set_stats(compute_stats(col));
    }

    void transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            const double scaled = value / denom;
            value = apply_clip(scaled);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            value = value * denom;
        }
    }
};

class LogStandardScalerImpl final : public BaseScaler {
public:
    explicit LogStandardScalerImpl(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::LogStandard, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        std::vector<double> log_values;
        log_values.reserve(col.size());
        for (double value : col) {
            if (!std::isfinite(value) || value <= -1.0) {
                continue;
            }
            log_values.push_back(std::log1p(value));
        }
        set_stats(compute_stats(log_values));
    }

    void transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value) || value <= -1.0) {
                continue;
            }
            const double log_value = std::log1p(value);
            const double scaled = (log_value - stats_.mean) / denom;
            value = apply_clip(scaled);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.std);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            const double restored = (value * denom) + stats_.mean;
            value = std::expm1(restored);
        }
    }
};

class MinMaxScalerImpl final : public BaseScaler {
public:
    explicit MinMaxScalerImpl(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::MinMax, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        set_stats(compute_stats(col));
    }

    void transform(std::vector<double>& col) const override {
        const double range = stats_.max - stats_.min;
        const double denom = safe_denominator(range);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            const double scaled = (value - stats_.min) / denom;
            value = apply_clip(scaled);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        const double range = stats_.max - stats_.min;
        const double denom = safe_denominator(range);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            value = (value * denom) + stats_.min;
        }
    }
};

class RobustScalerImpl final : public BaseScaler {
public:
    explicit RobustScalerImpl(std::optional<std::pair<double, double>> clip)
        : BaseScaler(ScalerKind::Robust, std::move(clip)) {}

    void fit(const std::vector<double>& col) override {
        set_stats(compute_stats(col));
    }

    void transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.iqr);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            const double scaled = (value - stats_.median) / denom;
            value = apply_clip(scaled);
        }
    }

    void inverse_transform(std::vector<double>& col) const override {
        const double denom = safe_denominator(stats_.iqr);
        for (double& value : col) {
            if (!std::isfinite(value)) {
                continue;
            }
            value = (value * denom) + stats_.median;
        }
    }
};

} // namespace

void Scaler::fit_transform(std::vector<double>& col) {
    fit(col);
    transform(col);
}

std::unique_ptr<Scaler> make_scaler(
    ScalerKind kind,
    std::optional<std::pair<double, double>> clip
) {
    switch (kind) {
        case ScalerKind::None:
            return std::make_unique<IdentityScaler>(std::move(clip));
        case ScalerKind::Standard:
            return std::make_unique<StandardScalerImpl>(std::move(clip));
        case ScalerKind::ZeroStandard:
            return std::make_unique<ZeroStandardScalerImpl>(std::move(clip));
        case ScalerKind::LogStandard:
            return std::make_unique<LogStandardScalerImpl>(std::move(clip));
        case ScalerKind::MinMax:
            return std::make_unique<MinMaxScalerImpl>(std::move(clip));
        case ScalerKind::Robust:
            return std::make_unique<RobustScalerImpl>(std::move(clip));
        default:
            throw std::invalid_argument("Unknown ScalerKind");
    }
}

std::string scaler_kind_to_string(ScalerKind kind) {
    switch (kind) {
        case ScalerKind::None: return "none";
        case ScalerKind::Standard: return "standard";
        case ScalerKind::ZeroStandard: return "zero_standard";
        case ScalerKind::LogStandard: return "log_standard";
        case ScalerKind::MinMax: return "min_max";
        case ScalerKind::Robust: return "robust";
        default: return "unknown";
    }
}

std::optional<ScalerKind> scaler_kind_from_string(std::string_view name) {
    std::string lowered(name.begin(), name.end());
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lowered == "none") return ScalerKind::None;
    if (lowered == "standard") return ScalerKind::Standard;
    if (lowered == "zero_standard" || lowered == "zerostandard") return ScalerKind::ZeroStandard;
    if (lowered == "log_standard" || lowered == "logstandard") return ScalerKind::LogStandard;
    if (lowered == "min_max" || lowered == "minmax") return ScalerKind::MinMax;
    if (lowered == "robust") return ScalerKind::Robust;
    return std::nullopt;
}

nlohmann::json column_stats_to_json(const ColumnStats& stats) {
    nlohmann::json j;
    j["mean"] = stats.mean;
    j["std"] = stats.std;
    j["median"] = stats.median;
    j["iqr"] = stats.iqr;
    j["min"] = stats.min;
    j["max"] = stats.max;
    j["n_samples"] = stats.n_samples;
    return j;
}

ColumnStats column_stats_from_json(const nlohmann::json& json) {
    ColumnStats stats;
    stats.mean = json.at("mean").get<double>();
    stats.std = json.at("std").get<double>();
    stats.median = json.at("median").get<double>();
    stats.iqr = json.at("iqr").get<double>();
    stats.min = json.at("min").get<double>();
    stats.max = json.at("max").get<double>();
    stats.n_samples = json.at("n_samples").get<std::size_t>();
    return stats;
}

nlohmann::json scaler_params_to_json(const ScalerParams& params) {
    nlohmann::json j;
    j["kind"] = scaler_kind_to_string(params.kind);
    j["stats"] = column_stats_to_json(params.stats);
    if (params.clip_lo.has_value() || params.clip_hi.has_value()) {
        nlohmann::json clip_json;
        if (params.clip_lo.has_value()) {
            clip_json["lo"] = *params.clip_lo;
        }
        if (params.clip_hi.has_value()) {
            clip_json["hi"] = *params.clip_hi;
        }
        j["clip"] = clip_json;
    }
    return j;
}

ScalerParams scaler_params_from_json(const nlohmann::json& json) {
    ScalerParams params;
    const auto kind_str = json.at("kind").get<std::string>();
    auto kind = scaler_kind_from_string(kind_str);
    if (!kind.has_value()) {
        throw std::invalid_argument("Unknown scaler kind: " + kind_str);
    }
    params.kind = *kind;
    params.stats = column_stats_from_json(json.at("stats"));
    if (json.contains("clip") && json["clip"].is_object()) {
        const auto& clip = json["clip"];
        if (clip.contains("lo") && !clip["lo"].is_null()) {
            params.clip_lo = clip["lo"].get<double>();
        }
        if (clip.contains("hi") && !clip["hi"].is_null()) {
            params.clip_hi = clip["hi"].get<double>();
        }
    }
    return params;
}

namespace {
double apply_clip(double value, const ScalerParams& params) {
    if (params.clip_lo.has_value()) {
        value = std::max(value, *params.clip_lo);
    }
    if (params.clip_hi.has_value()) {
        value = std::min(value, *params.clip_hi);
    }
    return value;
}

double safe_denominator_value(double value) {
    return (std::abs(value) < kEpsilon) ? 1.0 : value;
}
} // namespace

double apply_scaler_value(double value, const ScalerParams& params) {
    if (!std::isfinite(value)) {
        return value;
    }

    switch (params.kind) {
        case ScalerKind::None:
            return apply_clip(value, params);
        case ScalerKind::Standard: {
            const double denom = safe_denominator_value(params.stats.std);
            const double scaled = (value - params.stats.mean) / denom;
            return apply_clip(scaled, params);
        }
        case ScalerKind::ZeroStandard: {
            const double denom = safe_denominator_value(params.stats.std);
            const double scaled = value / denom;
            return apply_clip(scaled, params);
        }
        case ScalerKind::LogStandard: {
            if (value <= -1.0) {
                return value;
            }
            const double log_value = std::log1p(value);
            const double denom = safe_denominator_value(params.stats.std);
            const double scaled = (log_value - params.stats.mean) / denom;
            return apply_clip(scaled, params);
        }
        case ScalerKind::MinMax: {
            const double range = params.stats.max - params.stats.min;
            const double denom = safe_denominator_value(range);
            const double scaled = (value - params.stats.min) / denom;
            return apply_clip(scaled, params);
        }
        case ScalerKind::Robust: {
            const double denom = safe_denominator_value(params.stats.iqr);
            const double scaled = (value - params.stats.median) / denom;
            return apply_clip(scaled, params);
        }
        default:
            return value;
    }
}

double inverse_apply_scaler_value(double value, const ScalerParams& params) {
    if (!std::isfinite(value)) {
        return value;
    }

    switch (params.kind) {
        case ScalerKind::None:
            return value;
        case ScalerKind::Standard: {
            const double denom = safe_denominator_value(params.stats.std);
            return (value * denom) + params.stats.mean;
        }
        case ScalerKind::ZeroStandard: {
            const double denom = safe_denominator_value(params.stats.std);
            return value * denom;
        }
        case ScalerKind::LogStandard: {
            const double denom = safe_denominator_value(params.stats.std);
            const double restored = (value * denom) + params.stats.mean;
            return std::expm1(restored);
        }
        case ScalerKind::MinMax: {
            const double range = params.stats.max - params.stats.min;
            const double denom = safe_denominator_value(range);
            return (value * denom) + params.stats.min;
        }
        case ScalerKind::Robust: {
            const double denom = safe_denominator_value(params.stats.iqr);
            return (value * denom) + params.stats.median;
        }
        default:
            return value;
    }
}

FittedScaler::FittedScaler(ScalerParams params)
    : params_(std::move(params)) {}

double FittedScaler::transform(double value) const {
    return apply_scaler_value(value, params_);
}

double FittedScaler::inverse_transform(double value) const {
    return inverse_apply_scaler_value(value, params_);
}

const ScalerParams& FittedScaler::params() const {
    return params_;
}

double inverse_transform_value(const FittedScaler& scaler, double value) {
    return scaler.inverse_transform(value);
}

std::string ScalerStore::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [column, params] : by_column) {
        nlohmann::json entry;
        entry["column"] = column;
        entry["params"] = scaler_params_to_json(params);
        arr.push_back(std::move(entry));
    }
    return arr.dump(2);
}

std::optional<ScalerStore> ScalerStore::from_json(const std::string& json_str) {
    try {
        auto parsed = nlohmann::json::parse(json_str);
        if (!parsed.is_array()) {
            return std::nullopt;
        }
        ScalerStore store;
        for (const auto& entry : parsed) {
            const auto& column = entry.at("column").get_ref<const std::string&>();
            const auto& params_json = entry.at("params");
            ScalerParams params = scaler_params_from_json(params_json);
            store.by_column.emplace(column, std::move(params));
        }
        return store;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

} // namespace rivulet
