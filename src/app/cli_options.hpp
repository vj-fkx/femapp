#pragma once

#include <span>

namespace fem::app {

// Options handled before the Qt application object exists.
struct CliOptions
{
    bool showVersion = false; // --version: print the version and exit
    bool smokeTest = false;   // --smoke-test: open the main window, then quit
};

// Parses the raw process arguments (including argv[0]). Unknown arguments are
// ignored so that Qt can still consume its own options.
[[nodiscard]] CliOptions parseCliOptions(std::span<const char* const> args);

} // namespace fem::app
