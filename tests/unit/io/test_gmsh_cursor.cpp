#include "gmsh_cursor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <clocale>
#include <cstdint>
#include <cstring>
#include <string>

using Catch::Matchers::ContainsSubstring;
using fem::io::detail::GmshCursor;
using fem::io::detail::parseDouble;

// REQ-003-22
TEST_CASE("ASCII cursor reads tokens, numbers and lines", "[io][cursor]")
{
    GmshCursor cursor("4.1 0 8\n  -12  3.5e-2 \n$EndX\n", "sample.msh");
    REQUIRE(cursor.token() == std::string_view("4.1"));
    REQUIRE(cursor.readInt() == 0);
    REQUIRE(cursor.readSize() == 8U);
    REQUIRE(cursor.readInt() == -12);
    REQUIRE(cursor.readDouble() == 3.5e-2);
    CHECK(cursor.position() == "line 2");
    CHECK(cursor.expectEnd("X").has_value());
    cursor.skipWhitespace();
    CHECK(cursor.atEnd());
}

// REQ-003-22
TEST_CASE("ASCII errors give the line of the bad token", "[io][cursor]")
{
    GmshCursor cursor("1\n2\n3x\n", "bad.msh");
    REQUIRE(cursor.readInt() == 1);
    REQUIRE(cursor.readInt() == 2);
    const auto bad = cursor.readInt();
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message,
               ContainsSubstring("bad.msh, line 3") && ContainsSubstring("invalid integer '3x'"));

    GmshCursor empty("  \n", "empty.msh");
    const auto missing = empty.readDouble();
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("unexpected end of file"));

    GmshCursor negative("-4", "negative.msh");
    CHECK_FALSE(negative.readSize().has_value());
}

// REQ-003-22
TEST_CASE("binary cursor reads raw values and reports bytes", "[io][cursor]")
{
    std::string data(4 + 8 + 8, '\0');
    const std::int32_t one = 1;
    const std::uint64_t size = 1234567890123ULL;
    const double value = -2.75;
    std::memcpy(data.data(), &one, 4);
    std::memcpy(data.data() + 4, &size, 8);
    std::memcpy(data.data() + 12, &value, 8);

    GmshCursor cursor(data, "sample_bin.msh");
    cursor.setBinary(true);
    REQUIRE(cursor.readInt() == 1);
    REQUIRE(cursor.readSize() == size);
    REQUIRE(cursor.readDouble() == value);
    CHECK(cursor.position() == "byte 20");

    const auto past = cursor.readInt();
    REQUIRE_FALSE(past.has_value());
    CHECK_THAT(past.error().message,
               ContainsSubstring("byte 20") && ContainsSubstring("unexpected end of file"));
}

// REQ-003-18
TEST_CASE("unknown sections are skipped up to their end marker", "[io][cursor]")
{
    GmshCursor cursor("anything\n$Nodes inside\n$EndCustom\nnext\n", "s.msh");
    REQUIRE(cursor.skipSection("Custom").has_value());
    CHECK(cursor.line() == std::string_view("next"));

    GmshCursor unterminated("anything\n", "s.msh");
    CHECK_FALSE(unterminated.skipSection("Custom").has_value());
}

// Numbers must not depend on the C locale (a decimal comma in pt_BR).
TEST_CASE("decimal numbers ignore the C locale", "[io][cursor]")
{
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    const bool switched = std::setlocale(LC_NUMERIC, "pt_BR.UTF-8") != nullptr ||
                          std::setlocale(LC_NUMERIC, "Portuguese_Brazil.1252") != nullptr ||
                          std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr;
    INFO("comma locale available: " << switched);

    double value = 0.0;
    CHECK(parseDouble("1.5", value));
    CHECK(value == 1.5);
    CHECK(parseDouble("-0.3999999999991138", value));
    CHECK(value == -0.3999999999991138);
    CHECK(parseDouble("1e-05", value));
    CHECK(value == 1e-05);
    CHECK(parseDouble("+2", value));
    CHECK(value == 2.0);
    CHECK_FALSE(parseDouble("1,5", value));
    CHECK_FALSE(parseDouble("1.0x", value));
    CHECK_FALSE(parseDouble("", value));

    std::setlocale(LC_NUMERIC, previous.c_str());
}
