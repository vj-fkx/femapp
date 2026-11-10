#pragma once

#include <string_view>

namespace fem::core {

struct Version
{
    int major;
    int minor;
    int patch;
};

// Application version, defined once in the top-level CMakeLists.txt.
[[nodiscard]] Version version() noexcept;

// Same version as "major.minor.patch".
[[nodiscard]] std::string_view versionString() noexcept;

} // namespace fem::core
