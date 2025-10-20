//
// window_manifest.cpp
//

#include "internal/window_manifest.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace rivulet {

namespace {

struct ManifestHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t reserved;
    std::uint64_t row_count;
};

static constexpr char kMagic[8] = {'R', 'I', 'N', 'D', 'W', 'M', 'P', 'Q'};
static constexpr std::uint32_t kVersion = 1u;
static constexpr std::int64_t kNullValue = std::numeric_limits<std::int64_t>::min();

static_assert(sizeof(ManifestHeader) == 24, "ManifestHeader has unexpected padding");

ManifestHeader make_header(std::uint64_t row_count) {
    ManifestHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.reserved = 0;
    header.row_count = row_count;
    return header;
}

bool validate_header(const ManifestHeader& header) {
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (header.version != kVersion) {
        return false;
    }
    return true;
}

void write_row(std::ostream& os, const WindowRow& row) {
    std::int64_t values[4];
    values[0] = row.window_start;
    values[1] = row.window_end;
    values[2] = row.target_start.has_value() ? *row.target_start : kNullValue;
    values[3] = row.target_end.has_value() ? *row.target_end : kNullValue;
    os.write(reinterpret_cast<const char*>(values), sizeof(values));
}

bool read_row(std::istream& is, WindowRow* row) {
    std::int64_t values[4];
    is.read(reinterpret_cast<char*>(values), sizeof(values));
    if (!is) {
        return false;
    }
    row->window_start = values[0];
    row->window_end = values[1];
    if (values[2] != kNullValue) {
        row->target_start = values[2];
    } else {
        row->target_start.reset();
    }
    if (values[3] != kNullValue) {
        row->target_end = values[3];
    } else {
        row->target_end.reset();
    }
    return true;
}

std::string make_errno_message(const char* what) {
    std::ostringstream oss;
    oss << what << ": " << std::strerror(errno);
    return oss.str();
}

}  // namespace

bool write_windows_manifest_parquet(const std::string& path,
                                    const std::vector<WindowRow>& rows,
                                    std::string* error_msg) {
    namespace fs = std::filesystem;

    auto fail = [&](const std::string& message) -> bool {
        if (error_msg) {
            *error_msg = message;
        }
        return false;
    };

    try {
        fs::path dst(path);
        if (dst.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            if (ec) {
                return fail("Failed to create directory '" + dst.parent_path().string() + "': " + ec.message());
            }
        }

        fs::path tmp = dst;
        tmp += ".tmp";

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                return fail(make_errno_message("Failed to open temp file for writing"));
            }

            ManifestHeader header = make_header(rows.size());
            ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (!ofs.good()) {
                return fail(make_errno_message("Failed while writing manifest header"));
            }

            for (const auto& row : rows) {
                write_row(ofs, row);
                if (!ofs.good()) {
                    return fail(make_errno_message("Failed while writing manifest row"));
                }
            }

            ofs.flush();
            if (!ofs.good()) {
                return fail(make_errno_message("Failed to flush manifest to disk"));
            }
        }

        std::error_code ec;
        fs::rename(tmp, dst, ec);
        if (ec) {
            fs::copy_file(tmp, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::error_code copy_ec = ec;
                fs::remove(tmp, ec);
                return fail("Failed to move temp file into place: " + copy_ec.message());
            }
            fs::remove(tmp, ec);
        }

        if (error_msg) {
            error_msg->clear();
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(std::string("Exception in write_windows_manifest_parquet: ") + ex.what());
    }
}

bool append_windows_manifest_parquet(const std::string& path,
                                     const WindowRow& row,
                                     std::string* error_msg) {
    return append_windows_manifest_parquet_batch(path, {row}, error_msg);
}

bool append_windows_manifest_parquet_batch(const std::string& path,
                                           const std::vector<WindowRow>& rows,
                                           std::string* error_msg) {
    namespace fs = std::filesystem;

    auto fail = [&](const std::string& message) -> bool {
        if (error_msg) {
            *error_msg = message;
        }
        return false;
    };

    if (rows.empty()) {
        if (error_msg) {
            error_msg->clear();
        }
        return true;
    }

    try {
        fs::path dst(path);
        if (dst.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            if (ec) {
                return fail("Failed to create directory '" + dst.parent_path().string() + "': " + ec.message());
            }
        }

        if (!fs::exists(dst)) {
            return write_windows_manifest_parquet(path, rows, error_msg);
        }

        std::fstream stream(dst, std::ios::binary | std::ios::in | std::ios::out);
        if (!stream.is_open()) {
            return fail(make_errno_message("Failed to open manifest for appending"));
        }

        ManifestHeader header{};
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream.good() || !validate_header(header)) {
            return fail("Invalid manifest header when appending: " + dst.string());
        }

        stream.seekp(0, std::ios::end);
        if (!stream.good()) {
            return fail(make_errno_message("Failed to seek to end of manifest"));
        }

        for (const auto& row : rows) {
            write_row(stream, row);
            if (!stream.good()) {
                return fail(make_errno_message("Failed while appending manifest row"));
            }
        }

        header.row_count += rows.size();
        stream.seekp(0, std::ios::beg);
        if (!stream.good()) {
            return fail(make_errno_message("Failed to seek to manifest header"));
        }
        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!stream.good()) {
            return fail(make_errno_message("Failed while updating manifest header"));
        }
        stream.flush();
        if (!stream.good()) {
            return fail(make_errno_message("Failed to flush appended manifest"));
        }

        if (error_msg) {
            error_msg->clear();
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(std::string("Exception in append_windows_manifest_parquet_batch: ") + ex.what());
    }
}

bool read_windows_manifest_parquet(const std::string& path,
                                   std::vector<WindowRow>* rows,
                                   std::string* error_msg) {
    auto fail = [&](const std::string& message) -> bool {
        if (error_msg) {
            *error_msg = message;
        }
        return false;
    };

    if (!rows) {
        return fail("rows pointer cannot be null");
    }

    try {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return fail(make_errno_message("Failed to open manifest for reading"));
        }

        ManifestHeader header{};
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!ifs.good() || !validate_header(header)) {
            return fail("Invalid manifest header while reading: " + path);
        }

        rows->clear();
        if (header.row_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail("Manifest row_count exceeds system capacity (SIZE_MAX)");
        }
        rows->reserve(static_cast<std::size_t>(header.row_count));

        for (std::uint64_t i = 0; i < header.row_count; ++i) {
            WindowRow row;
            row.ticker.clear();
            if (!read_row(ifs, &row)) {
                return fail("Truncated manifest while reading rows: " + path);
            }
            rows->push_back(std::move(row));
        }

        if (error_msg) {
            error_msg->clear();
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(std::string("Exception in read_windows_manifest_parquet: ") + ex.what());
    }
}

}  // namespace rivulet

