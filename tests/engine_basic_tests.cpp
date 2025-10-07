//
// Created by Eric Gilerson on 10/6/25.
//

#include <catch2/catch_all.hpp>
#include "rivulet/engine.hpp"
using namespace rivulet;

static Timestamp ns(long long x) { return Timestamp(std::chrono::nanoseconds(x)); }

TEST_CASE("basic_feature_at_mean_price") {
    Engine eng({.store_path="data/", .watermark_delay=std::chrono::milliseconds(2000), .max_window_samples=16});
    std::string err;
    REQUIRE(eng.load_feature_spec(R"({"features":[]})", err));

    REQUIRE(eng.ingest(Event{"X", ns(1000), {{"price", 10.0}}}, err));
    REQUIRE(eng.ingest(Event{"X", ns(2000), {{"price", 20.0}}}, err));
    REQUIRE(eng.ingest(Event{"X", ns(3000), {{"price", 30.0}}}, err));

    auto fv = eng.feature_at("X", ns(2500), err);
    REQUIRE(fv.has_value());
    REQUIRE(fv->names.size() == 2);
    CHECK(fv->values[0] == 2.0);               // count
    CHECK(fv->values[1] == Approx(15.0));      // mean of 10 and 20
}
