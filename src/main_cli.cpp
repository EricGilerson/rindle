//
// Created by Eric Gilerson on 10/6/25.
//
// Rivulet CLI - Complete example demonstrating the public API

#include "rivulet.hpp"
#include <iostream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

void print_usage(const char* program_name) {
    std::cout << "Rivulet Dataset Builder - CLI Example\n\n";
    std::cout << "Usage: " << program_name << " <input_dir> <output_dir>\n\n";
    std::cout << "This example:\n";
    std::cout << "  1. Creates a dataset configuration\n";
    std::cout << "  2. Builds the dataset from CSV files\n";
    std::cout << "  3. Loads the dataset tensors into memory\n\n";
    std::cout << "Expected input format:\n";
    std::cout << "  - CSV files in <input_dir>, one per ticker (e.g., AAPL.csv, MSFT.csv)\n";
    std::cout << "  - Each CSV must have: Date,Open,High,Low,Close,Volume columns\n";
    std::cout << "  - Date format: Unix timestamp in nanoseconds (UTC)\n\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    fs::path input_dir(argv[1]);
    fs::path output_dir(argv[2]);

    // Validate input directory exists
    if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
        std::cerr << "Error: Input directory does not exist or is not a directory: "
                  << input_dir << "\n";
        return 1;
    }

    std::cout << "=== Rivulet Dataset Builder Example ===\n\n";

    //==========================================================================
    // Step 1: Create Configuration
    //==========================================================================
    std::cout << "[1/3] Creating dataset configuration...\n";

    // Define feature columns to use
    std::vector<std::string> feature_columns = {
        "Open", "High", "Low", "Close", "Volume"
    };

    // Create configuration
    auto config_result = rivulet::create_config(
        input_dir,                          // Input directory with CSV files
        output_dir,                         // Output directory for processed data
        feature_columns,                    // Features to extract
        60,                                 // seq_length: 60 timesteps per window
        5,                                  // future_horizon: predict 5 steps ahead
        "Close",                            // target_column: predict Close price
        rivulet::TimeMode::UTC_NS,         // Use UTC nanosecond timestamps
        false                               // time-major layout (not row-major)
    );

    if (!config_result.status.ok()) {
        std::cerr << "Error creating config: " << config_result.status.message() << "\n";
        return 1;
    }

    rivulet::DatasetConfig config = config_result.value;
    std::cout << "  ✓ Configuration created\n";
    std::cout << "    - Features: " << feature_columns.size() << " columns\n";
    std::cout << "    - Window size: " << config.seq_length << " timesteps\n";
    std::cout << "    - Prediction horizon: " << config.future_horizon << " steps\n";
    std::cout << "    - Target: " << config.target_column.value_or("None") << "\n\n";

    //==========================================================================
    // Step 2: Build Dataset
    //==========================================================================
    std::cout << "[2/3] Building dataset...\n";
    std::cout << "  Processing CSV files from: " << input_dir << "\n";

    auto build_result = rivulet::build_dataset(config);

    if (!build_result.status.ok()) {
        std::cerr << "Error building dataset: " << build_result.status.message() << "\n";
        return 1;
    }

    rivulet::ManifestContent manifest = build_result.value;
    std::cout << "  ✓ Dataset built successfully\n";
    std::cout << "    - Tickers processed: " << manifest.total_tickers << "\n";
    std::cout << "    - Total windows created: " << manifest.total_windows << "\n";
    std::cout << "    - Total input rows: " << manifest.total_input_rows << "\n";
    std::cout << "    - Output directory: " << manifest.output_dir << "\n\n";

    // Print per-ticker statistics
    std::cout << "  Per-ticker breakdown:\n";
    for (const auto& stats : manifest.ticker_stats) {
        std::cout << "    " << std::setw(8) << std::left << stats.ticker
                  << " - Windows: " << std::setw(6) << stats.num_windows
                  << " | Rows processed: " << std::setw(6) << stats.rows_processed
                  << " | Rows dropped: " << stats.rows_dropped << "\n";
    }
    std::cout << "\n";

    //==========================================================================
    // Step 3: Load Dataset into Memory
    //==========================================================================
    std::cout << "[3/3] Loading dataset tensors...\n";

    auto dataset_result = rivulet::get_dataset(manifest);

    if (!dataset_result.status.ok()) {
        std::cerr << "Error loading dataset: " << dataset_result.status.message() << "\n";
        return 1;
    }

    rivulet::Dataset dataset = dataset_result.value;
    std::cout << "  ✓ Dataset loaded into memory\n";
    std::cout << "    - Feature tensor shape: [" << dataset.features.size() << "]\n";
    std::cout << "    - Target tensor shape: [" << dataset.targets.size() << "]\n";
    std::cout << "    - Metadata entries: " << dataset.metadata.size() << "\n\n";

    //==========================================================================
    // Display Sample Data
    //==========================================================================
    std::cout << "=== Sample Data ===\n";

    if (!dataset.metadata.empty()) {
        const auto& first_window = dataset.metadata[0];
        std::cout << "First window:\n";
        std::cout << "  Ticker: " << first_window.ticker << "\n";
        std::cout << "  Window start: " << first_window.window_start_ns << " ns\n";
        std::cout << "  Window end: " << first_window.window_end_ns << " ns\n";
        std::cout << "  Row in dataset: " << first_window.row_in_dataset << "\n";

        // Show first few feature values
        std::size_t feature_size = config.seq_length * feature_columns.size();
        std::size_t samples_to_show = std::min(size_t(10), dataset.features.size());

        std::cout << "\n  First " << samples_to_show << " feature values:\n    ";
        for (std::size_t i = 0; i < samples_to_show; ++i) {
            std::cout << std::fixed << std::setprecision(4) << dataset.features[i];
            if (i < samples_to_show - 1) std::cout << ", ";
        }
        std::cout << "\n";

        // Show first target value if available
        if (!dataset.targets.empty()) {
            std::cout << "\n  First target value: "
                      << std::fixed << std::setprecision(4)
                      << dataset.targets[0] << "\n";
        }
    }

    std::cout << "\n=== Success! ===\n";
    std::cout << "Dataset is ready for training. The manifest and processed data\n";
    std::cout << "have been saved to: " << output_dir << "\n";
    std::cout << "\nYou can now use the feature and target tensors for model training.\n";

    return 0;
}