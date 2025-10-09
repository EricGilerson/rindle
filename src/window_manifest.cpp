//
// Created by Eric Gilerson on 10/8/25.
//

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <cstring>
#include "rivulet/window_manifest.hpp"

namespace rivulet {


bool write_windows_manifest_csv(const std::string& path,
                                const std::vector<WindowRow>& rows,
                                std::string* error_msg) {
    namespace fs = std::filesystem;

    auto fail = [&](const char* what) -> bool {
        if (error_msg) {
            std::ostringstream oss;
            oss << what << ": " << std::strerror(errno);
            *error_msg = oss.str();
        }
        return false;
    };

    try {
        // Ensure parent directory exists
        fs::path dst(path);
        if (dst.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            if (ec) {
                if (error_msg) {
                    std::ostringstream oss;
                    oss << "Failed to create directory '" << dst.parent_path().string()
                        << "': " << ec.message();
                    *error_msg = oss.str();
                }
                return false;
            }
        }

        // Write to a temp file first (atomic replace)
        fs::path tmp = dst;
        tmp += ".tmp";

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                return fail("Failed to open temp file for writing");
            }

            // Header
            ofs << WindowRow::csv_header() << "\n";
            if (!ofs.good()) {
                return fail("Failed while writing CSV header");
            }

            // Rows
            for (const auto& r : rows) {
                ofs << r.to_csv_row() << "\n";
                if (!ofs.good()) {
                    return fail("Failed while writing CSV row");
                }
            }

            ofs.flush();
            if (!ofs.good()) {
                return fail("Failed to flush CSV to disk");
            }
        }

        // Atomically replace destination
        std::error_code ec;
        fs::rename(tmp, dst, ec);
        if (ec) {
            // If atomic rename across devices fails, fall back to copy+replace
            fs::copy_file(tmp, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error_msg) {
                    std::ostringstream oss;
                    oss << "Failed to move temp file into place: " << ec.message();
                    *error_msg = oss.str();
                }
                // Best effort: try to remove temp file
                std::error_code ec2;
                fs::remove(tmp, ec2);
                return false;
            }
            // Remove temp file after successful copy
            std::error_code ec2;
            fs::remove(tmp, ec2);
        }

        if (error_msg) {
            error_msg->clear();
        }
        return true;

    } catch (const std::exception& ex) {
        if (error_msg) {
            *error_msg = std::string("Exception in write_windows_manifest_csv: ") + ex.what();
        }
        return false;
    }
}

} // namespace rivulet
