#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rindle/dataset_types.hpp"
#include "rindle/types.hpp"
#include "rindle/manifest_types.hpp"
#include "internal/window_manifest.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

using Catch::Approx;
namespace rv = rivulet;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path()
             / ("rindle_types_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

} // namespace

// ── Tensor3D ─────────────────────────────────────────────────────────────

TEST_CASE("Tensor3D default construction is empty", "[tensor]") {
    rv::Tensor3D t;
    CHECK(t.windows == 0);
    CHECK(t.seq_len == 0);
    CHECK(t.features == 0);
    CHECK(t.data.empty());
}

TEST_CASE("Tensor3D parameterized construction allocates correctly", "[tensor]") {
    rv::Tensor3D t(4, 10, 3);
    CHECK(t.size() == 120);
    CHECK(t.data.size() == 120);
    CHECK(t.windows == 4);
    CHECK(t.seq_len == 10);
    CHECK(t.features == 3);
}

TEST_CASE("Tensor3D at read/write consistency", "[tensor]") {
    rv::Tensor3D t(2, 3, 4);
    t.at(1, 2, 3) = 42.0f;
    CHECK(t.at(1, 2, 3) == Approx(42.0f));

    const auto& ct = t;
    CHECK(ct.at(1, 2, 3) == Approx(42.0f));
}

TEST_CASE("Tensor3D offset layout is row-major [W][S][F]", "[tensor]") {
    rv::Tensor3D t(3, 5, 7);
    for (std::int64_t w = 0; w < 3; ++w) {
        for (std::int64_t s = 0; s < 5; ++s) {
            for (std::int64_t f = 0; f < 7; ++f) {
                auto expected = static_cast<std::size_t>((w * 5 + s) * 7 + f);
                CHECK(t.offset(w, s, f) == expected);
            }
        }
    }
}

TEST_CASE("Tensor3D window_ptr points to correct start", "[tensor]") {
    rv::Tensor3D t(4, 10, 3);
    for (std::int64_t w = 0; w < 4; ++w) {
        auto expected_offset = static_cast<std::size_t>(w * 10 * 3);
        CHECK(t.window_ptr(w) == t.data.data() + expected_offset);
    }
}

TEST_CASE("Tensor3D reshape changes dimensions and resizes", "[tensor]") {
    rv::Tensor3D t(2, 3, 4);
    CHECK(t.size() == 24);
    t.reshape(5, 6, 2);
    CHECK(t.windows == 5);
    CHECK(t.seq_len == 6);
    CHECK(t.features == 2);
    CHECK(t.size() == 60);
}

// ── Dataset ──────────────────────────────────────────────────────────────

TEST_CASE("Dataset accessors delegate to X tensor", "[dataset]") {
    rv::Dataset ds;
    ds.X = rv::Tensor3D(10, 50, 4);
    CHECK(ds.n_windows() == 10);
    CHECK(ds.seq_length() == 50);
    CHECK(ds.n_features() == 4);
}

TEST_CASE("Dataset aligned_by_window_and_time", "[dataset]") {
    rv::Dataset ds;
    ds.X = rv::Tensor3D(10, 50, 4);

    SECTION("aligned when same W and S") {
        ds.Y = rv::Tensor3D(10, 50, 1);
        CHECK(ds.aligned_by_window_and_time());
    }
    SECTION("not aligned when different W") {
        ds.Y = rv::Tensor3D(5, 50, 1);
        CHECK_FALSE(ds.aligned_by_window_and_time());
    }
    SECTION("not aligned when different S") {
        ds.Y = rv::Tensor3D(10, 25, 1);
        CHECK_FALSE(ds.aligned_by_window_and_time());
    }
}

TEST_CASE("Dataset clear resets all fields", "[dataset]") {
    rv::Dataset ds;
    ds.X = rv::Tensor3D(10, 50, 4);
    ds.Y = rv::Tensor3D(10, 50, 1);
    ds.meta.resize(10);
    ds.clear();

    CHECK(ds.X.windows == 0);
    CHECK(ds.Y.windows == 0);
    CHECK(ds.meta.empty());
}

// ── Result / Status ──────────────────────────────────────────────────────

TEST_CASE("Result is truthy when ok with value", "[result]") {
    rv::Result<int> r{42, rv::Status::OK()};
    CHECK(static_cast<bool>(r));
    REQUIRE(r.value.has_value());
    CHECK(*r.value == 42);
}

TEST_CASE("Result is falsy on error", "[result]") {
    rv::Result<int> r{std::nullopt, rv::Status::Error("bad")};
    CHECK_FALSE(static_cast<bool>(r));
    CHECK_FALSE(r.status.ok);
    CHECK(r.status.message == "bad");
}

TEST_CASE("Status factories", "[result]") {
    auto ok = rv::Status::OK();
    CHECK(ok.ok);
    CHECK(ok.message.empty());

    auto err = rv::Status::Error("whoops");
    CHECK_FALSE(err.ok);
    CHECK(err.message == "whoops");
}

// ── ManifestContent ──────────────────────────────────────────────────────

TEST_CASE("ManifestContent build_ticker_index populates lookup", "[manifest]") {
    rv::ManifestContent mc;
    for (const auto& t : {"AAPL", "GOOG", "MSFT"}) {
        rv::TickerStats ts;
        ts.ticker = t;
        mc.ticker_stats.push_back(std::move(ts));
    }
    mc.build_ticker_index();
    CHECK(mc.ticker_index.size() == 3);
    CHECK(mc.ticker_index.count("AAPL") == 1);
    CHECK(mc.ticker_index.count("GOOG") == 1);
    CHECK(mc.ticker_index.count("MSFT") == 1);
}

TEST_CASE("ManifestContent find_stats returns pointer for existing ticker", "[manifest]") {
    rv::ManifestContent mc;
    rv::TickerStats ts;
    ts.ticker = "AAPL";
    ts.input_rows = 1000;
    mc.ticker_stats.push_back(ts);
    mc.build_ticker_index();

    const rv::TickerStats* found = mc.find_stats("AAPL");
    REQUIRE(found != nullptr);
    CHECK(found->ticker == "AAPL");
    CHECK(found->input_rows == 1000);
}

TEST_CASE("ManifestContent find_stats returns nullptr for missing", "[manifest]") {
    rv::ManifestContent mc;
    mc.build_ticker_index();
    CHECK(mc.find_stats("ZZZZ") == nullptr);
}

// ── Window Manifest binary roundtrip ─────────────────────────────────────

TEST_CASE("Window manifest write + read roundtrip", "[window_manifest]") {
    TempDir tmp;
    std::string path = (tmp.path / "test.parquet").string();

    std::vector<rv::WindowRow> rows;
    rows.push_back(rv::WindowRow{"AAPL", 0, 49, 50, 51});
    rows.push_back(rv::WindowRow{"AAPL", 1, 50, std::nullopt, std::nullopt});
    rows.push_back(rv::WindowRow{"GOOG", 10, 59, 60, 61});

    std::string err;
    REQUIRE(rv::write_windows_manifest_parquet(path, rows, &err));

    std::vector<rv::WindowRow> loaded;
    REQUIRE(rv::read_windows_manifest_parquet(path, &loaded, &err));
    REQUIRE(loaded.size() == 3);

    CHECK(loaded[0].window_start == 0);
    CHECK(loaded[0].window_end == 49);
    CHECK(loaded[1].window_start == 1);
    CHECK(loaded[1].window_end == 50);
    CHECK(loaded[2].window_start == 10);
    CHECK(loaded[2].window_end == 59);
}

TEST_CASE("Window manifest read from nonexistent file returns false", "[window_manifest]") {
    std::vector<rv::WindowRow> rows;
    std::string err;
    CHECK_FALSE(rv::read_windows_manifest_parquet("/nonexistent/file.parquet", &rows, &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Window manifest write to invalid path returns false", "[window_manifest]") {
    std::vector<rv::WindowRow> rows;
    rows.push_back(rv::WindowRow{"X", 0, 1, std::nullopt, std::nullopt});
    std::string err;
    CHECK_FALSE(rv::write_windows_manifest_parquet("/nonexistent/dir/file.parquet", rows, &err));
}
