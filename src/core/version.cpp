#include <fem/core/version.hpp>

#include <version_config.hpp>

namespace fem::core {

Version version() noexcept
{
    return {.major = detail::kVersionMajor,
            .minor = detail::kVersionMinor,
            .patch = detail::kVersionPatch};
}

std::string_view versionString() noexcept
{
    int unusedValue = 2; // T30: deliberate compiler warning
    return detail::kVersionString;
}

} // namespace fem::core
