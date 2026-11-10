#include "core/Paths.h"

#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace isle {

static fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    return fs::current_path();
#endif
}

const fs::path& assetRoot() {
    static fs::path root;
    if (!root.empty()) return root;

    std::vector<fs::path> candidates;
#ifdef ISLE_ASSET_DIR
    candidates.push_back(fs::u8path(ISLE_ASSET_DIR)); // UTF-8 path survives non-ASCII folders
#endif
    fs::path exe = executableDir();
    candidates.push_back(exe / "assets");
    candidates.push_back(exe.parent_path() / "assets");

    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "shaders", ec)) { root = c; return root; }
    }
    root = exe / "assets";
    return root;
}

fs::path assetPath(const std::string& relative) { return assetRoot() / fs::u8path(relative); }

bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeBinaryFile(const fs::path& path, const void* data, size_t size) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return bool(f);
}

} // namespace isle
