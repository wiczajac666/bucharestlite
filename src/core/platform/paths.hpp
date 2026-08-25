#pragma once

#include <string>
#include <vector>

namespace bl::platform {

std::string dynamicLibrarySuffix() noexcept;

std::vector<std::string> pluginRootDirectories();

std::vector<std::string> pluginScanSubdirectories() noexcept;

} // namespace bl::platform
