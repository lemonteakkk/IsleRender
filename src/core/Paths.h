#pragma once
#include <filesystem>
#include <string>

namespace isle {

// Root folder holding shaders/ etc. Resolved once: source tree (for hot reload) first,
// then next to the executable (for release builds).
const std::filesystem::path& assetRoot();
std::filesystem::path assetPath(const std::string& relative);

bool readTextFile(const std::filesystem::path& path, std::string& out);
bool writeBinaryFile(const std::filesystem::path& path, const void* data, size_t size);

} // namespace isle
