#include "../include/rindle.hpp"
#include <iostream>
#include <iomanip>

using std::cout;
using std::cerr;
using std::endl;

static void print_last_x_sequence(const rivulet::Dataset& dataset) {
    const auto& X = dataset.X;
    if (X.windows == 0) {
        cout << "\nNo X windows available to print.\n";
        return;
    }

    const std::size_t widx = X.windows - 1; // last window
    cout << "\n============================================================\n";
    cout << "Last X sequence (window index " << widx << ")"
         << " with shape [" << X.seq_len << ", " << X.features << "]\n";
    cout << "Each row below is one timestep; values are comma-separated features.\n";
    cout << "============================================================\n";

    cout << std::setprecision(6) << std::fixed;
    for (std::size_t t = 0; t < X.seq_len; ++t) {
        cout << "t=" << t << ": ";
        for (std::size_t f = 0; f < X.features; ++f) {
            cout << X.at(widx, t, f);
            if (f + 1 < X.features) cout << ", ";
        }
        cout << "\n";
    }
}

int main() {
    using namespace rivulet;

    //==========================================================================
    // Step 1: Create configuration
    //==========================================================================
    cout << "Creating configuration...\n";

    auto config_result = create_config(
        "data/raw",                              // input_dir
        "data/processed",                        // output_dir
        {"Price_0939", "Prev_Delta_Close", "Gap", "Composite_HL"}, // feature_columns
        50,                                      // seq_length (L)
        1,                                       // future_horizon (H)
        "Delta_Close",                           // target_column
        TimeMode::UTC_NS,                        // time_mode
        false,                                   // row_major
        ScalerKind::Standard                     // scaler_kind applied to features
    );

    if (!config_result) {
        cerr << "Config creation failed: " << config_result.status.message << "\n";
        return 1;
    }

    DatasetConfig config = std::move(*config_result.value);
    cout << "Configuration created successfully!\n";

    //==========================================================================
    // Step 2: Build dataset (reads files, cleans, builds windows, creates manifest)
    //==========================================================================
    cout << "\nBuilding dataset...\n";

    auto build_result = build_dataset(config);

    if (!build_result) {
        cerr << "Build failed: " << build_result.status.message << "\n";
        return 1;
    }

    ManifestContent manifest = std::move(*build_result.value);

    cout << "Dataset built successfully!\n";
    cout << "  Total tickers: " << manifest.total_tickers << "\n";
    cout << "  Total windows: " << manifest.total_windows << "\n";
    cout << "  Total input rows: " << manifest.total_input_rows << "\n";
    cout << "  Feature scaler: " << scaler_kind_to_string(manifest.scaler_kind) << "\n";

    //==========================================================================
    // Step 3: Get dataset tensors (loads actual data into memory)
    //==========================================================================
    cout << "\nLoading dataset tensors...\n";

    // Option A: Use in-memory manifest
    auto dataset_result = get_dataset(manifest);

    // Option B: Load from saved manifest.json
    // auto dataset_result = get_dataset("data/processed/manifest.json");

    if (!dataset_result) {
        cerr << "Load failed: " << dataset_result.status.message << "\n";
        return 1;
    }

    Dataset dataset = std::move(*dataset_result.value);

    cout << "Dataset loaded successfully!\n";
    cout << "  X shape: [" << dataset.X.windows << ", "
         << dataset.X.seq_len << ", " << dataset.X.features << "]\n";
    cout << "  Y shape: [" << dataset.Y.windows << ", "
         << dataset.Y.seq_len << ", " << dataset.Y.features << "]\n";

    // Access a value (unchanged from your example)
    float first_value = dataset.X.at(0, 0, 0);
    cout << "  First X value: " << first_value << "\n";
    if (dataset.Y.windows > 0) {
        float first_target = dataset.Y.at(0, 0, 0);
        cout << "  First Y value: " << first_target << "\n";
    }

    if (!dataset.meta.empty() && !manifest.feature_columns.empty()) {
        const std::string& first_ticker = dataset.meta.front().ticker;
        const std::string& first_feature = manifest.feature_columns.front();
        auto scaler_result = get_feature_scaler(manifest, first_ticker, first_feature);
        if (scaler_result) {
            double scaled = static_cast<double>(dataset.X.at(0, 0, 0));
            double original = inverse_transform_value(*scaler_result.value, scaled);
            cout << "  Inverse-transformed first X value: " << original << "\n";
        } else {
            cout << "  Could not fetch scaler for inverse transform: "
                 << scaler_result.status.message << "\n";
        }
    }

    //==========================================================================
    // New: Print the last X window/sequence
    //==========================================================================
    print_last_x_sequence(dataset);

    return 0;
}
