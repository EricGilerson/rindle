#include <rivulet.hpp>
#include <iostream>

int main() {
    using namespace rivulet;
    
    //==========================================================================
    // Step 1: Create configuration
    //==========================================================================
    
    std::cout << "Creating configuration...\n";
    
    auto config_result = create_config(
        "data/raw",                              // input_dir
        "data/processed",                         // output_dir
        {"Open", "High", "Low", "Close", "Volume"}, // feature_columns
        50,                                       // seq_length (L)
        1,                                        // future_horizon (H)
        "Close",                                  // target_column
        TimeMode::UTC_NS,                         // time_mode
        false                                     // row_major
    );
    
    if (!config_result) {
        std::cerr << "Config creation failed: " << config_result.status.message << "\n";
        return 1;
    }
    
    DatasetConfig config = std::move(*config_result.value);
    std::cout << "Configuration created successfully!\n";
    
    //==========================================================================
    // Step 2: Build dataset (reads files, cleans, builds windows, creates manifest)
    //==========================================================================
    
    std::cout << "\nBuilding dataset...\n";
    
    auto build_result = build_dataset(config);
    
    if (!build_result) {
        std::cerr << "Build failed: " << build_result.status.message << "\n";
        return 1;
    }
    
    ManifestContent manifest = std::move(*build_result.value);
    
    std::cout << "Dataset built successfully!\n";
    std::cout << "  Total tickers: " << manifest.total_tickers << "\n";
    std::cout << "  Total windows: " << manifest.total_windows << "\n";
    std::cout << "  Total input rows: " << manifest.total_input_rows << "\n";
    
    //==========================================================================
    // Step 3: Get dataset tensors (loads actual data into memory)
    //==========================================================================
    
    std::cout << "\nLoading dataset tensors...\n";
    
    // Option A: Use in-memory manifest
    auto dataset_result = get_dataset(manifest);
    
    // Option B: Load from saved manifest.json
    // auto dataset_result = get_dataset("data/processed/manifest.json");
    
    if (!dataset_result) {
        std::cerr << "Load failed: " << dataset_result.status.message << "\n";
        return 1;
    }
    
    Dataset dataset = std::move(*dataset_result.value);
    
    std::cout << "Dataset loaded successfully!\n";
    std::cout << "  X shape: [" << dataset.X.windows << ", " 
              << dataset.X.seq_len << ", " << dataset.X.features << "]\n";
    std::cout << "  Y shape: [" << dataset.Y.windows << ", " 
              << dataset.Y.seq_len << ", " << dataset.Y.features << "]\n";
    
    // Access data
    float first_value = dataset.X.at(0, 0, 0);
    std::cout << "  First X value: " << first_value << "\n";
    
    if (dataset.Y.windows > 0) {
        float first_target = dataset.Y.at(0, 0, 0);
        std::cout << "  First Y value: " << first_target << "\n";
    }
    
    return 0;
}
