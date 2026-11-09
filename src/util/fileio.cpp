#include "util/fileio.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace docmerge::util {

bool readFileBytes(const std::string& path, std::string& outContent, std::string& outError) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        outError = "failed to open file for reading: " + path;
        return false;
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    if (in.bad()) {
        outError = "I/O error while reading file: " + path;
        return false;
    }
    outContent = oss.str();
    return true;
}

bool writeFileAtomic(const std::string& path, std::string_view content, std::string& outError) {
    std::error_code ec;
    const std::filesystem::path targetPath(path);
    std::filesystem::path parentDir = targetPath.parent_path();
    if (parentDir.empty()) {
        parentDir = ".";
    }

    const std::filesystem::path tempPath =
        parentDir / (targetPath.filename().string() + ".docmerge.tmp");

    {
        std::ofstream out(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "failed to open temporary file for writing: " + tempPath.string();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            outError = "failed to write bytes to temporary file: " + tempPath.string();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec) {
        outError = "failed to rename temporary file over target '" + path + "': " + ec.message();
        std::error_code removeEc;
        std::filesystem::remove(tempPath, removeEc);
        return false;
    }
    return true;
}

bool containsNulByte(std::string_view content) noexcept {
    return content.find('\0') != std::string_view::npos;
}

}  // namespace docmerge::util
