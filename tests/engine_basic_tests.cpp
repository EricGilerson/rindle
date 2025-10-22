//
// Created by Eric Gilerson on 10/19/25.
//

#include <catch2/catch_test_macros.hpp>

#include "rindle.hpp"

namespace {

rivulet::ManifestContent make_manifest_with_scaler() {
    rivulet::ManifestContent manifest;
    manifest.feature_columns = {"close"};

    rivulet::TickerStats stats;
    stats.ticker = "AAPL";
    stats.scaler_kind = rivulet::ScalerKind::Standard;

    rivulet::ScalerParams params;
    params.kind = rivulet::ScalerKind::Standard;
    params.stats.mean = 10.0;
    params.stats.std = 2.0;

    rivulet::FeatureScalerParams feature_params;
    feature_params.feature = "close";
    feature_params.params = params;

    stats.feature_scalers.push_back(feature_params);
    manifest.ticker_stats.push_back(stats);
    manifest.build_ticker_index();

    return manifest;
}

} // namespace

TEST_CASE("get_feature_scaler returns fitted scaler", "[scaler]") {
    auto manifest = make_manifest_with_scaler();

    auto scaler_result = rivulet::get_feature_scaler(manifest, "AAPL", "close");
    REQUIRE(scaler_result);

    const rivulet::FittedScaler& scaler = *scaler_result.value;

    const double raw_value = 12.0;
    const double scaled = scaler.transform(raw_value);
    CHECK(scaled == Approx((raw_value - 10.0) / 2.0));

    const double restored = rivulet::inverse_transform_value(scaler, scaled);
    CHECK(restored == Approx(raw_value));
}

TEST_CASE("get_feature_scaler reports lookup errors", "[scaler]") {
    rivulet::ManifestContent manifest;
    manifest.build_ticker_index();

    auto missing_ticker = rivulet::get_feature_scaler(manifest, "MSFT", "close");
    REQUIRE_FALSE(missing_ticker);
    CHECK_FALSE(missing_ticker.status.ok);

    auto populated = make_manifest_with_scaler();
    auto missing_feature = rivulet::get_feature_scaler(populated, "AAPL", "volume");
    REQUIRE_FALSE(missing_feature);
    CHECK_FALSE(missing_feature.status.ok);
}
