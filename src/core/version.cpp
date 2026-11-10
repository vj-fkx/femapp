#include <fem/core/version.hpp>

#include <version_config.hpp>

namespace fem::core {

Version version() noexcept
{
    return {FEMAPP_VERSION_MAJOR, FEMAPP_VERSION_MINOR, FEMAPP_VERSION_PATCH};
}

std::string_view versionString() noexcept
{
    return FEMAPP_VERSION_STRING;
}

} // namespace fem::core
