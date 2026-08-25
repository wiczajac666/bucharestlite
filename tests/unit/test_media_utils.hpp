#pragma once

#include <filesystem>
#include <string>

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace bltest {

inline std::string mediaPath(const std::string& fileName) {
    return (std::filesystem::path(BL_TEST_MEDIA_DIR) / fileName).string();
}

} // namespace bltest
