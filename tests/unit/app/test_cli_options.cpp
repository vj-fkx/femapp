#include "cli_options.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>

using fem::app::CliOptions;
using fem::app::parseCliOptions;

// REQ-001-14, REQ-001-15
TEST_CASE("command line flags are recognised", "[app][cli]")
{
    const std::array<const char*, 3> args = {"femapp", "--version", "--smoke-test"};
    const CliOptions options = parseCliOptions(args);
    CHECK(options.showVersion);
    CHECK(options.smokeTest);
    CHECK_FALSE(options.meshFile.has_value());
}

// REQ-004-03: the first .msh argument, in any case, is the mesh to import.
TEST_CASE("a mesh file on the command line is recognised", "[app][cli]")
{
    const std::array<const char*, 6> args = {"femapp",       "-platform",  "offscreen",
                                             "C:/Malhas/Viga.MSH", "other.msh", "--smoke-test"};
    const CliOptions options = parseCliOptions(args);
    CHECK(options.smokeTest);
    CHECK(options.meshFile == std::optional<std::string>("C:/Malhas/Viga.MSH"));

    const std::array<const char*, 3> none = {"femapp", ".msh", "notes.txt"};
    CHECK_FALSE(parseCliOptions(none).meshFile.has_value());

    CHECK_FALSE(parseCliOptions({}).meshFile.has_value());
}
