//
// Created by Eric Gilerson on 10/6/25.
//

#include "rivulet/engine.hpp"
#include <iostream>
#include <sstream>

using namespace rivulet;

static rivulet::Timestamp parse_ns(long long ns_since_epoch) {
    return rivulet::Timestamp(std::chrono::nanoseconds(ns_since_epoch));
}

int main() {
    Engine eng({.store_path = "data/", .watermark_delay = std::chrono::milliseconds(2000), .max_window_samples = 2048});
    std::string err;
    eng.load_feature_spec(R"({"features":[]})", err);

    // toy ingest: three events for key "AAPL"
    eng.ingest(Event{"AAPL", parse_ns(1000000), {{"price", 194.0}}}, err);
    eng.ingest(Event{"AAPL", parse_ns(2000000), {{"price", 195.0}}}, err);
    eng.ingest(Event{"AAPL", parse_ns(3000000), {{"price", 196.0}}}, err);

    auto fv = eng.feature_at("AAPL", parse_ns(2500000), err);
    if (!fv) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    std::cout << "Features at t=2.5ms ns:\n";
    for (std::size_t i = 0; i < fv->names.size(); ++i) {
        std::cout << "  " << fv->names[i] << " = " << fv->values[i] << "\n";
    }
    return 0;
}
