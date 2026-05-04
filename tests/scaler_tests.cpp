#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "rindle/scaler.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace rv = rivulet;

static const std::vector<double> kData = {1.0, 2.0, 3.0, 4.0, 5.0};

// ── Factory ──────────────────────────────────────────────────────────────

TEST_CASE("make_scaler returns non-null for each ScalerKind", "[scaler]") {
    auto kind = GENERATE(
        rv::ScalerKind::None,
        rv::ScalerKind::Standard,
        rv::ScalerKind::ZeroStandard,
        rv::ScalerKind::LogStandard,
        rv::ScalerKind::MinMax,
        rv::ScalerKind::Robust
    );
    auto scaler = rv::make_scaler(kind);
    REQUIRE(scaler != nullptr);
}

// ── Per-kind roundtrips ──────────────────────────────────────────────────

TEST_CASE("Standard scaler roundtrip", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::Standard);
    std::vector<double> col = kData;
    scaler->fit(col);

    auto params = scaler->params();
    CHECK(params.stats.mean == Approx(3.0));

    scaler->transform(col);
    CHECK(col[0] == Approx((1.0 - 3.0) / params.stats.std));

    scaler->inverse_transform(col);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(col[i] == Approx(kData[i]));
    }
}

TEST_CASE("ZeroStandard scaler roundtrip", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::ZeroStandard);
    std::vector<double> col = kData;
    scaler->fit(col);
    auto params = scaler->params();

    scaler->transform(col);
    CHECK(col[0] == Approx(1.0 / params.stats.std));

    scaler->inverse_transform(col);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(col[i] == Approx(kData[i]));
    }
}

TEST_CASE("LogStandard scaler roundtrip", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::LogStandard);
    std::vector<double> col = kData;
    scaler->fit(col);

    std::vector<double> transformed = col;
    scaler->transform(transformed);

    scaler->inverse_transform(transformed);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(transformed[i] == Approx(kData[i]));
    }
}

TEST_CASE("MinMax scaler roundtrip", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::MinMax);
    std::vector<double> col = kData;
    scaler->fit(col);

    scaler->transform(col);
    CHECK(col[0] == Approx(0.0));
    CHECK(col[4] == Approx(1.0));

    scaler->inverse_transform(col);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(col[i] == Approx(kData[i]));
    }
}

TEST_CASE("Robust scaler roundtrip", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::Robust);
    std::vector<double> col = kData;
    scaler->fit(col);
    auto params = scaler->params();

    scaler->transform(col);
    CHECK(col[2] == Approx((3.0 - params.stats.median) / params.stats.iqr));

    scaler->inverse_transform(col);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(col[i] == Approx(kData[i]));
    }
}

TEST_CASE("None scaler passes values through", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::None);
    std::vector<double> col = kData;
    scaler->fit(col);
    scaler->transform(col);
    for (std::size_t i = 0; i < kData.size(); ++i) {
        CHECK(col[i] == Approx(kData[i]));
    }
}

// ── Edge cases ───────────────────────────────────────────────────────────

TEST_CASE("Scaler handles constant column", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::Standard);
    std::vector<double> col = {5.0, 5.0, 5.0, 5.0};
    scaler->fit(col);
    auto params = scaler->params();
    CHECK(params.stats.std == Approx(1.0));

    scaler->transform(col);
    for (double v : col) {
        CHECK(std::isfinite(v));
    }
}

TEST_CASE("fit_transform equivalent to fit then transform", "[scaler]") {
    auto s1 = rv::make_scaler(rv::ScalerKind::Standard);
    auto s2 = rv::make_scaler(rv::ScalerKind::Standard);

    std::vector<double> col1 = kData;
    s1->fit(col1);
    s1->transform(col1);

    std::vector<double> col2 = kData;
    s2->fit_transform(col2);

    REQUIRE(col1.size() == col2.size());
    for (std::size_t i = 0; i < col1.size(); ++i) {
        CHECK(col1[i] == Approx(col2[i]));
    }
}

TEST_CASE("Scaler with clipping bounds", "[scaler]") {
    auto scaler = rv::make_scaler(rv::ScalerKind::Standard, std::make_pair(-1.0, 1.0));
    std::vector<double> col = {-100.0, 0.0, 3.0, 100.0};
    scaler->fit(col);
    scaler->transform(col);

    for (double v : col) {
        CHECK(v >= -1.0);
        CHECK(v <= 1.0);
    }
}

// ── Free functions ───────────────────────────────────────────────────────

TEST_CASE("apply_scaler_value matches Scaler::transform", "[scaler]") {
    auto kind = GENERATE(
        rv::ScalerKind::Standard,
        rv::ScalerKind::ZeroStandard,
        rv::ScalerKind::MinMax,
        rv::ScalerKind::Robust
    );

    auto scaler = rv::make_scaler(kind);
    std::vector<double> col = kData;
    scaler->fit(col);
    auto params = scaler->params();

    std::vector<double> transformed = col;
    scaler->transform(transformed);

    for (std::size_t i = 0; i < col.size(); ++i) {
        double applied = rv::apply_scaler_value(kData[i], params);
        CHECK(applied == Approx(transformed[i]));
    }
}

TEST_CASE("inverse_apply_scaler_value roundtrips with apply_scaler_value", "[scaler]") {
    auto kind = GENERATE(
        rv::ScalerKind::Standard,
        rv::ScalerKind::ZeroStandard,
        rv::ScalerKind::MinMax,
        rv::ScalerKind::Robust
    );

    auto scaler = rv::make_scaler(kind);
    std::vector<double> col = kData;
    scaler->fit(col);
    auto params = scaler->params();

    for (double val : kData) {
        double scaled = rv::apply_scaler_value(val, params);
        double restored = rv::inverse_apply_scaler_value(scaled, params);
        CHECK(restored == Approx(val));
    }
}

TEST_CASE("FittedScaler transform and inverse roundtrip", "[scaler]") {
    rv::ScalerParams params;
    params.kind = rv::ScalerKind::Standard;
    params.stats.mean = 10.0;
    params.stats.std = 2.0;

    rv::FittedScaler fitted(params);
    double raw = 14.0;
    double scaled = fitted.transform(raw);
    CHECK(scaled == Approx((14.0 - 10.0) / 2.0));
    CHECK(fitted.inverse_transform(scaled) == Approx(raw));
}

TEST_CASE("FittedScaler params returns stored params", "[scaler]") {
    rv::ScalerParams params;
    params.kind = rv::ScalerKind::Robust;
    params.stats.median = 5.0;
    params.stats.iqr = 3.0;

    rv::FittedScaler fitted(params);
    CHECK(fitted.params().kind == rv::ScalerKind::Robust);
    CHECK(fitted.params().stats.median == Approx(5.0));
    CHECK(fitted.params().stats.iqr == Approx(3.0));
}

// ── String conversion ────────────────────────────────────────────────────

TEST_CASE("scaler_kind_to_string and from_string roundtrip", "[scaler]") {
    auto kind = GENERATE(
        rv::ScalerKind::None,
        rv::ScalerKind::Standard,
        rv::ScalerKind::ZeroStandard,
        rv::ScalerKind::LogStandard,
        rv::ScalerKind::MinMax,
        rv::ScalerKind::Robust
    );
    auto str = rv::scaler_kind_to_string(kind);
    auto roundtripped = rv::scaler_kind_from_string(str);
    REQUIRE(roundtripped.has_value());
    CHECK(*roundtripped == kind);
}

TEST_CASE("scaler_kind_from_string returns nullopt for unknown", "[scaler]") {
    CHECK_FALSE(rv::scaler_kind_from_string("garbage").has_value());
}

// ── JSON serialization ───────────────────────────────────────────────────

TEST_CASE("ScalerParams JSON roundtrip", "[scaler]") {
    rv::ScalerParams params;
    params.kind = rv::ScalerKind::Standard;
    params.stats.mean = 42.0;
    params.stats.std = 7.0;
    params.stats.n_samples = 100;
    params.clip_lo = -3.0;
    params.clip_hi = 3.0;

    auto json = rv::scaler_params_to_json(params);
    auto restored = rv::scaler_params_from_json(json);

    CHECK(restored.kind == params.kind);
    CHECK(restored.stats.mean == Approx(params.stats.mean));
    CHECK(restored.stats.std == Approx(params.stats.std));
    CHECK(restored.stats.n_samples == params.stats.n_samples);
    REQUIRE(restored.clip_lo.has_value());
    CHECK(*restored.clip_lo == Approx(-3.0));
    REQUIRE(restored.clip_hi.has_value());
    CHECK(*restored.clip_hi == Approx(3.0));
}

TEST_CASE("ColumnStats JSON roundtrip", "[scaler]") {
    rv::ColumnStats stats;
    stats.mean = 1.0;
    stats.std = 2.0;
    stats.median = 1.5;
    stats.iqr = 1.0;
    stats.min = -5.0;
    stats.max = 10.0;
    stats.n_samples = 50;

    auto json = rv::column_stats_to_json(stats);
    auto restored = rv::column_stats_from_json(json);

    CHECK(restored.mean == Approx(stats.mean));
    CHECK(restored.std == Approx(stats.std));
    CHECK(restored.median == Approx(stats.median));
    CHECK(restored.iqr == Approx(stats.iqr));
    CHECK(restored.min == Approx(stats.min));
    CHECK(restored.max == Approx(stats.max));
    CHECK(restored.n_samples == stats.n_samples);
}

TEST_CASE("ScalerStore JSON roundtrip", "[scaler]") {
    rv::ScalerStore store;
    rv::ScalerParams p1;
    p1.kind = rv::ScalerKind::Standard;
    p1.stats.mean = 10.0;
    p1.stats.std = 2.0;

    rv::ScalerParams p2;
    p2.kind = rv::ScalerKind::MinMax;
    p2.stats.min = 0.0;
    p2.stats.max = 100.0;

    store.by_column["close"] = p1;
    store.by_column["volume"] = p2;

    std::string json_str = store.to_json();
    auto restored = rv::ScalerStore::from_json(json_str);
    REQUIRE(restored.has_value());
    CHECK(restored->by_column.size() == 2);
    CHECK(restored->by_column.at("close").kind == rv::ScalerKind::Standard);
    CHECK(restored->by_column.at("volume").kind == rv::ScalerKind::MinMax);
}

TEST_CASE("ScalerStore from_json returns nullopt on invalid", "[scaler]") {
    CHECK_FALSE(rv::ScalerStore::from_json("not json").has_value());
    CHECK_FALSE(rv::ScalerStore::from_json("{}").has_value());
}
