#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "rindle.hpp"
#include "rindle/scaler.hpp"
#include "internal/csv_io.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using Catch::Approx;
namespace rv = rivulet;
namespace fs = std::filesystem;

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

static const fs::path kDataRaw = fs::path(STRINGIFY(RINDLE_TEST_DATA_DIR)) / "raw";
static const std::vector<std::string> kFeatures = {
    "Price_0939", "Prev_Delta_Close", "Gap", "Composite_HL"
};
static const std::string kTarget = "Delta_Close";

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path()
             / ("rindle_integ_" + std::to_string(std::rand()));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

rv::Result<rv::DatasetConfig> make_config(const fs::path& output_dir) {
    return rv::create_config(kDataRaw, output_dir, kFeatures, 50, 1, kTarget,
                             rv::TimeMode::UTC_NS, false, rv::ScalerKind::Standard);
}

} // namespace

// ── create_config validation ─────────────────────────────────────────────

TEST_CASE("create_config succeeds with valid inputs", "[integration]") {
    TempDir tmp;
    auto result = make_config(tmp.path);
    REQUIRE(static_cast<bool>(result));
    CHECK(result.value->seq_length == 50);
    CHECK(result.value->future_horizon == 1);
    CHECK(result.value->feature_columns == kFeatures);
    CHECK(result.value->target_column.has_value());
    CHECK(*result.value->target_column == kTarget);
}

TEST_CASE("create_config fails for nonexistent input_dir", "[integration]") {
    TempDir tmp;
    auto result = rv::create_config("/nonexistent_dir_12345", tmp.path, kFeatures, 50, 1);
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.status.message.find("does not exist") != std::string::npos);
}

TEST_CASE("create_config fails for file instead of directory", "[integration]") {
    TempDir tmp;
    auto csv_path = kDataRaw / "ABNB.csv";
    auto result = rv::create_config(csv_path, tmp.path, kFeatures, 50, 1);
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.status.message.find("not a directory") != std::string::npos);
}

TEST_CASE("create_config fails for empty features", "[integration]") {
    TempDir tmp;
    auto result = rv::create_config(kDataRaw, tmp.path, {}, 50, 1);
    CHECK_FALSE(static_cast<bool>(result));
}

TEST_CASE("create_config fails for seq_length zero", "[integration]") {
    TempDir tmp;
    auto result = rv::create_config(kDataRaw, tmp.path, kFeatures, 0, 1);
    CHECK_FALSE(static_cast<bool>(result));
}

TEST_CASE("create_config fails for future_horizon zero", "[integration]") {
    TempDir tmp;
    auto result = rv::create_config(kDataRaw, tmp.path, kFeatures, 50, 0);
    CHECK_FALSE(static_cast<bool>(result));
}

TEST_CASE("create_config creates output directory if missing", "[integration]") {
    TempDir tmp;
    auto subdir = tmp.path / "nested" / "output";
    auto result = rv::create_config(kDataRaw, subdir, kFeatures, 50, 1);
    REQUIRE(static_cast<bool>(result));
    CHECK(fs::exists(subdir));
}

// ── Full pipeline ────────────────────────────────────────────────────────

TEST_CASE("build_dataset processes all 3 tickers", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));

    const auto& m = *manifest_result.value;
    CHECK(m.total_tickers == 3);
    CHECK(m.total_windows > 0);

    CHECK(m.find_stats("ABNB") != nullptr);
    CHECK(m.find_stats("ABBV") != nullptr);
    CHECK(m.find_stats("ABT") != nullptr);

    CHECK(fs::exists(tmp.path / "manifest.json"));
    CHECK(fs::exists(tmp.path / "ABNB_windows.parquet"));
    CHECK(fs::exists(tmp.path / "ABBV_windows.parquet"));
    CHECK(fs::exists(tmp.path / "ABT_windows.parquet"));
}

TEST_CASE("build_dataset manifest fields are consistent", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));

    const auto& m = *manifest_result.value;
    CHECK(m.seq_length == 50);
    CHECK(m.feature_columns == kFeatures);
    CHECK(m.scaler_kind == rv::ScalerKind::Standard);

    std::size_t sum_windows = 0;
    for (const auto& ts : m.ticker_stats) {
        sum_windows += ts.windows_created;
    }
    CHECK(sum_windows == m.total_windows);
}

TEST_CASE("get_dataset loads tensors with correct shapes", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto ds_result = rv::get_dataset(m, 1.0, 1);
    REQUIRE(static_cast<bool>(ds_result));
    const auto& ds = *ds_result.value;

    CHECK(ds.n_windows() == static_cast<std::int64_t>(m.total_windows));
    CHECK(ds.seq_length() == 50);
    CHECK(ds.n_features() == static_cast<std::int64_t>(kFeatures.size()));
    CHECK(ds.Y.features == 1);
    CHECK(ds.meta.size() == m.total_windows);
}

TEST_CASE("get_dataset with subsampling returns fewer windows", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto ds_result = rv::get_dataset(m, 0.5, 1);
    REQUIRE(static_cast<bool>(ds_result));
    const auto& ds = *ds_result.value;

    CHECK(ds.n_windows() < static_cast<std::int64_t>(m.total_windows));
    CHECK(ds.n_windows() > 0);
}

TEST_CASE("get_dataset from manifest path produces same shapes", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto ds_result = rv::get_dataset(tmp.path / "manifest.json", 1.0, 1);
    REQUIRE(static_cast<bool>(ds_result));
    CHECK(ds_result.value->n_windows() == static_cast<std::int64_t>(m.total_windows));
}

TEST_CASE("get_dataset validates percentage bounds", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    CHECK_FALSE(static_cast<bool>(rv::get_dataset(m, 0.0, 1)));
    CHECK_FALSE(static_cast<bool>(rv::get_dataset(m, 1.5, 1)));
}

TEST_CASE("build with no target column", "[integration]") {
    TempDir tmp;
    auto config = rv::create_config(kDataRaw, tmp.path, kFeatures, 50, 1,
                                    std::nullopt, rv::TimeMode::UTC_NS, false,
                                    rv::ScalerKind::Standard);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto ds_result = rv::get_dataset(m, 1.0, 1);
    REQUIRE(static_cast<bool>(ds_result));
    const auto& ds = *ds_result.value;

    CHECK(ds.n_features() == static_cast<std::int64_t>(kFeatures.size()));
    CHECK(ds.Y.size() == 0);
}

// ── Scaler verification ──────────────────────────────────────────────────

TEST_CASE("get_feature_scaler returns valid scaler", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto scaler_result = rv::get_feature_scaler(m, "ABNB", "Price_0939");
    REQUIRE(static_cast<bool>(scaler_result));

    const auto& scaler = *scaler_result.value;
    CHECK(scaler.params().kind == rv::ScalerKind::Standard);

    double raw = 150.0;
    double scaled = scaler.transform(raw);
    double restored = scaler.inverse_transform(scaled);
    CHECK(restored == Approx(raw));
}

TEST_CASE("get_feature_scaler from path works", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));
    rv::build_dataset(*config.value, 1);

    auto scaler_result = rv::get_feature_scaler(tmp.path / "manifest.json", "ABNB", "Price_0939");
    REQUIRE(static_cast<bool>(scaler_result));
    CHECK(scaler_result.value->params().kind == rv::ScalerKind::Standard);
}

// ── Dataset content validation ───────────────────────────────────────────

TEST_CASE("dataset meta tracks provenance", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));

    auto ds_result = rv::get_dataset(*manifest_result.value, 1.0, 1);
    REQUIRE(static_cast<bool>(ds_result));
    const auto& ds = *ds_result.value;

    for (const auto& meta : ds.meta) {
        CHECK((meta.ticker == "ABNB" || meta.ticker == "ABBV" || meta.ticker == "ABT"));
        CHECK(meta.start_row >= 0);
        CHECK(meta.end_row >= meta.start_row);
    }
}

// ── Threading determinism ────────────────────────────────────────────────

TEST_CASE("single vs multi-thread build produces same manifest", "[integration]") {
    TempDir tmp1;
    TempDir tmp2;

    auto c1 = rv::create_config(kDataRaw, tmp1.path, kFeatures, 50, 1, kTarget);
    auto c2 = rv::create_config(kDataRaw, tmp2.path, kFeatures, 50, 1, kTarget);
    REQUIRE(static_cast<bool>(c1));
    REQUIRE(static_cast<bool>(c2));

    auto m1 = rv::build_dataset(*c1.value, 1);
    auto m2 = rv::build_dataset(*c2.value, 4);
    REQUIRE(static_cast<bool>(m1));
    REQUIRE(static_cast<bool>(m2));

    CHECK(m1.value->total_windows == m2.value->total_windows);
    CHECK(m1.value->total_input_rows == m2.value->total_input_rows);
    CHECK(m1.value->total_tickers == m2.value->total_tickers);

    for (const auto& ts1 : m1.value->ticker_stats) {
        const auto* ts2 = m2.value->find_stats(ts1.ticker);
        REQUIRE(ts2 != nullptr);
        CHECK(ts1.input_rows == ts2->input_rows);
        CHECK(ts1.windows_created == ts2->windows_created);

        REQUIRE(ts1.feature_scalers.size() == ts2->feature_scalers.size());
        for (std::size_t f = 0; f < ts1.feature_scalers.size(); ++f) {
            CHECK(ts1.feature_scalers[f].params.stats.mean ==
                  Approx(ts2->feature_scalers[f].params.stats.mean));
            CHECK(ts1.feature_scalers[f].params.stats.std ==
                  Approx(ts2->feature_scalers[f].params.stats.std));
        }
    }
}

TEST_CASE("single vs multi-thread get_dataset produces same tensors", "[integration]") {
    TempDir tmp;
    auto config = make_config(tmp.path);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;

    auto ds1_result = rv::get_dataset(m, 1.0, 1);
    auto ds2_result = rv::get_dataset(m, 1.0, 4);
    REQUIRE(static_cast<bool>(ds1_result));
    REQUIRE(static_cast<bool>(ds2_result));

    const auto& ds1 = *ds1_result.value;
    const auto& ds2 = *ds2_result.value;

    REQUIRE(ds1.n_windows() == ds2.n_windows());
    REQUIRE(ds1.seq_length() == ds2.seq_length());
    REQUIRE(ds1.n_features() == ds2.n_features());

    // Sort both by (ticker, start_row) for stable comparison
    struct IndexEntry {
        std::size_t idx;
        std::string ticker;
        std::int64_t start_row;
    };
    auto build_order = [](const rv::Dataset& ds) {
        std::vector<IndexEntry> entries;
        entries.reserve(ds.meta.size());
        for (std::size_t i = 0; i < ds.meta.size(); ++i) {
            entries.push_back({i, ds.meta[i].ticker, ds.meta[i].start_row});
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.ticker != b.ticker) return a.ticker < b.ticker;
            return a.start_row < b.start_row;
        });
        return entries;
    };

    auto order1 = build_order(ds1);
    auto order2 = build_order(ds2);

    const auto W = static_cast<std::size_t>(ds1.n_windows());
    bool all_match = true;
    for (std::size_t i = 0; i < W && all_match; ++i) {
        auto i1 = static_cast<std::int64_t>(order1[i].idx);
        auto i2 = static_cast<std::int64_t>(order2[i].idx);
        for (std::int64_t s = 0; s < ds1.seq_length() && all_match; ++s) {
            for (std::int64_t f = 0; f < ds1.n_features(); ++f) {
                if (ds1.X.at(i1, s, f) != ds2.X.at(i2, s, f)) {
                    all_match = false;
                    break;
                }
            }
        }
    }
    CHECK(all_match);
}

// ── ScalerKind variants ──────────────────────────────────────────────────

TEST_CASE("build with different scaler kinds", "[integration]") {
    auto kind = GENERATE(
        rv::ScalerKind::None,
        rv::ScalerKind::MinMax,
        rv::ScalerKind::Robust
    );

    TempDir tmp;
    auto config = rv::create_config(kDataRaw, tmp.path, kFeatures, 50, 1, kTarget,
                                    rv::TimeMode::UTC_NS, false, kind);
    REQUIRE(static_cast<bool>(config));

    auto manifest_result = rv::build_dataset(*config.value, 1);
    REQUIRE(static_cast<bool>(manifest_result));
    const auto& m = *manifest_result.value;
    CHECK(m.total_windows > 0);
    CHECK(m.scaler_kind == kind);

    auto ds_result = rv::get_dataset(m, 1.0, 1);
    REQUIRE(static_cast<bool>(ds_result));
    CHECK(ds_result.value->n_windows() == static_cast<std::int64_t>(m.total_windows));
}
