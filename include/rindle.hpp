#ifndef RIVULET_HPP
#define RIVULET_HPP

#pragma once

// Only include PUBLIC types that users need
#include "rindle/types.hpp"
#include "rindle/manifest_types.hpp"

// Forward declarations - users don't need to see internal types
namespace rivulet {
    class Manifest;
    class Catalog;
    class Driver;
}

namespace rivulet {

//==============================================================================
// PUBLIC API - Users can only call these 3 functions
//==============================================================================

/**
 * 1. Create dataset configuration
 *
 * This is the ONLY way to create a DatasetConfig for the library.
 * Forces users to provide all required parameters explicitly.
 *
 * @param input_dir Directory containing input CSV files (one per ticker)
 * @param output_dir Directory where processed dataset will be written
 * @param feature_columns List of column names to use as features
 * @param seq_length Number of timesteps in each window (L)
 * @param future_horizon Number of timesteps ahead for targets (H)
 * @param target_column Optional target column name (nullopt for unsupervised)
 * @param time_mode UTC_NS for timestamp-based, ORDINAL for row-index based
 * @param row_major Flatten order: false = time-major [W,S,F], true = row-major
 * @param scaler_kind Scaler applied to numeric features prior to windowing
 * @return Result containing DatasetConfig or error status
 */
Result<DatasetConfig> create_config(
    const std::filesystem::path& input_dir,
    const std::filesystem::path& output_dir,
    const std::vector<std::string>& feature_columns,
    std::size_t seq_length,
    std::size_t future_horizon,
    const std::optional<std::string>& target_column = std::nullopt,
    TimeMode time_mode = TimeMode::UTC_NS,
    bool row_major = false,
    ScalerKind scaler_kind = ScalerKind::Standard
);

/**
 * 2. Build dataset from configuration
 *
 * This performs the complete pipeline:
 * - Discovers all CSV files in input_dir
 * - Reads and scales each file
 * - Builds sliding windows
 * - Writes window manifests to output_dir
 * - Creates and saves manifest.json
 *
 * @param config Dataset configuration (must be created via create_config)
 * @return Result containing ManifestContent on success
 */
Result<ManifestContent> build_dataset(const DatasetConfig& config);

/**
 * 3. Get dataset tensors from built dataset
 *
 * This loads the actual data into memory:
 * - Reads window manifest CSVs
 * - Reads feature CSV files
 * - Constructs in-memory tensors (X and Y)
 * - Returns Dataset ready for training
 *
 * Can be called with either:
 * - manifest: In-memory manifest returned from build_dataset()
 * - manifest_path: Path to saved manifest.json file
 *
 * @param manifest In-memory manifest content
 * @param manifest_path Path to manifest.json file (alternative to manifest)
 * @return Result containing Dataset with X, Y tensors and metadata
 */
Result<Dataset> get_dataset(
    const ManifestContent& manifest
);

Result<Dataset> get_dataset(
    const std::filesystem::path& manifest_path
);

Result<FittedScaler> get_feature_scaler(
    const ManifestContent& manifest,
    const std::string& ticker,
    const std::string& feature
);

Result<FittedScaler> get_feature_scaler(
    const std::filesystem::path& manifest_path,
    const std::string& ticker,
    const std::string& feature
);

double inverse_transform_value(const FittedScaler& scaler, double value);

} // namespace rivulet

#endif // RIVULET_HPP
