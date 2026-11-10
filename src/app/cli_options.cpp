#include "cli_options.hpp"

#include <string_view>

namespace fem::app {

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
        }
    }
    return options;
}

} // namespace fem::app
