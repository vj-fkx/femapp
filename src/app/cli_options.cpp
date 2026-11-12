#include "cli_options.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace fem::app {

namespace {

bool endsWithMsh(std::string_view value)
{
    constexpr std::string_view kExtension = ".msh";
    if (value.size() <= kExtension.size()) {
        return false;
    }
    const std::string_view tail = value.substr(value.size() - kExtension.size());
    return std::ranges::equal(tail, kExtension, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == b;
    });
}

} // namespace

CliOptions parseCliOptions(std::span<const char* const> args)
{
    CliOptions options;
    if (args.empty()) {
        return options;
    }

    for (const char* arg : args.subspan(1)) {
        const std::string_view value = arg != nullptr ? arg : "";
        if (value == "--version") {
            options.showVersion = true;
        } else if (value == "--smoke-test") {
            options.smokeTest = true;
        } else if (!options.meshFile && endsWithMsh(value)) {
            options.meshFile = std::string(value);
        }
    }
    return options;
}

} // namespace fem::app
