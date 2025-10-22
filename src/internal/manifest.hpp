//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
File: src/internal/manifest.hpp

Overview:
    Declares the Manifest class that captures dataset build metadata and
    serializes it to and from JSON.

Key functionality:
    - Populate `ManifestContent` from a `DatasetConfig` and `Catalog` summary.
    - Persist manifest.json and reload it with validation helpers.
    - Offer ticker lookup utilities to assist window generation code at runtime.
==============================================================================*/

#ifndef RIVULET_MANIFEST_HPP
#define RIVULET_MANIFEST_HPP

#pragma once
#include "rindle/manifest_types.hpp"
#include <filesystem>
#include <string>
#include <vector>

#include "catalog.hpp"

namespace rivulet {

    class Manifest {
    public:
        Manifest() = default;
        explicit Manifest(const DatasetConfig& config, const Catalog& catalog);

        // Populate from config and catalog
        void populate(const DatasetConfig& config, const Catalog& catalog);

        // Serialize to JSON file
        bool write_to_file(const std::filesystem::path& path, std::string& error_msg) const;

        // Deserialize from JSON file
        static Result<Manifest> read_from_file(
            const std::filesystem::path &path
        );

        // Access content
        const ManifestContent& content() const { return content_; }
        ManifestContent& content_mut() { return content_; }

        const TickerMap& ticker_map() const;


    private:
        ManifestContent content_;

        mutable TickerMap ticker_map_cache_;

        std::string to_json() const;
        static std::optional<Manifest> from_json(const std::string &json_str, std::string &error_msg);
    };

} // namespace rivulet

#endif //RIVULET_MANIFEST_HPP
