#include <fem/geom/command_input.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <string_view>
#include <variant>

using fem::geom::AbsolutePoint;
using fem::geom::CommandName;
using fem::geom::Distance;
using fem::geom::Empty;
using fem::geom::Keyword;
using fem::geom::ParsedInput;
using fem::geom::PolarPoint;
using fem::geom::RelativePoint;
using fem::geom::Vec3;

namespace {

constexpr double kTolerance = 1e-12;

ParsedInput parsed(std::string_view text)
{
    const auto input = fem::geom::parseInput(text);
    INFO("input: " << text);
    REQUIRE(input.has_value());
    return input.value_or(ParsedInput{});
}

bool near(const Vec3& a, const Vec3& b)
{
    return (a - b).norm() < kTolerance;
}

Vec3 point(std::string_view text, const std::optional<Vec3>& last = std::nullopt,
           const Vec3& direction = Vec3::UnitX())
{
    const auto p = fem::geom::resolvePoint(parsed(text), last, direction);
    INFO("input: " << text);
    REQUIRE(p.has_value());
    return p.value_or(Vec3::Zero());
}

} // namespace

// REQ-005-10, 11: absolute and relative coordinates, 2D and 3D.
TEST_CASE("cartesian coordinates", "[geom][input]")
{
    CHECK(std::holds_alternative<AbsolutePoint>(parsed("3,4")));
    CHECK(near(point("3,4"), Vec3(3, 4, 0)));
    CHECK(near(point(" 1.5 , -2e1 , 0.25 "), Vec3(1.5, -20, 0.25)));
    CHECK(std::holds_alternative<RelativePoint>(parsed("@1,2")));
    CHECK(near(point("@1,2", Vec3(10, 10, 5)), Vec3(11, 12, 5)));
    CHECK(near(point("@ 0,0,-5", Vec3(1, 1, 5)), Vec3(1, 1, 0)));
    // A relative point needs a previous point.
    CHECK_FALSE(fem::geom::resolvePoint(parsed("@1,2"), std::nullopt, Vec3::UnitX()).has_value());
}

// REQ-005-12: polar coordinates, absolute and relative, in degrees.
TEST_CASE("polar coordinates", "[geom][input]")
{
    const auto polar = std::get<PolarPoint>(parsed("@5<90"));
    CHECK(polar.relative);
    CHECK(polar.distance == 5.0);
    CHECK(near(point("@5<90", Vec3(1, 1, 2)), Vec3(1, 6, 2)));
    CHECK(near(point("2<0"), Vec3(2, 0, 0)));
    CHECK(near(point("@2<225", Vec3::Zero()), Vec3(-std::sqrt(2.0), -std::sqrt(2.0), 0)));
    CHECK(near(point("@1<-90", Vec3(0, 0, 0)), Vec3(0, -1, 0)));
    CHECK_FALSE(fem::geom::resolvePoint(parsed("@1<0"), std::nullopt, Vec3::UnitX()).has_value());
}

// REQ-005-13: a single number is a distance along the cursor direction.
TEST_CASE("direct distance entry", "[geom][input]")
{
    CHECK(std::holds_alternative<Distance>(parsed("2.5")));
    CHECK(near(point("2.5", Vec3(1, 1, 1), Vec3(0, 3, 0)), Vec3(1, 3.5, 1)));
    CHECK_FALSE(fem::geom::resolvePoint(parsed("2"), std::nullopt, Vec3::UnitX()).has_value());
    CHECK_FALSE(fem::geom::resolvePoint(parsed("2"), Vec3::Zero(), Vec3::Zero()).has_value());
}

// REQ-005-05, REQ-005-08: keywords, empty input and malformed text.
TEST_CASE("keywords, empty and invalid input", "[geom][input]")
{
    CHECK(std::holds_alternative<Empty>(parsed("")));
    CHECK(std::holds_alternative<Empty>(parsed("   ")));
    CHECK(std::get<Keyword>(parsed(" line ")).text == "LINE");
    CHECK(std::get<Keyword>(parsed("c")).text == "C");
    for (const std::string_view bad :
         {"1,", "@", "3<", "<4", "a,b", "1,2,3,4", "1..2", "@x", "l1", "1 2"}) {
        INFO("input: " << bad);
        CHECK_FALSE(fem::geom::parseInput(bad).has_value());
    }
    // Keywords and Enter are not points.
    CHECK_FALSE(fem::geom::resolvePoint(parsed("LINE"), Vec3::Zero(), Vec3::UnitX()).has_value());
    CHECK_FALSE(fem::geom::resolvePoint(Empty{}, Vec3::Zero(), Vec3::UnitX()).has_value());
}

// REQ-005-05: names, aliases, any case; completion.
TEST_CASE("command names, aliases and completion", "[geom][input]")
{
    CHECK(fem::geom::findCommand("line") == CommandName::Line);
    CHECK(fem::geom::findCommand("L") == CommandName::Line);
    CHECK(fem::geom::findCommand("Pl") == CommandName::Pline);
    CHECK(fem::geom::findCommand("po") == CommandName::Point);
    CHECK(fem::geom::findCommand("m") == CommandName::Move);
    CHECK(fem::geom::findCommand("CO") == CommandName::Copy);
    CHECK(fem::geom::findCommand("cp") == CommandName::Copy);
    CHECK(fem::geom::findCommand("E") == CommandName::Erase);
    CHECK(fem::geom::findCommand("u") == CommandName::Undo);
    CHECK(fem::geom::findCommand("redo") == CommandName::Redo);
    CHECK_FALSE(fem::geom::findCommand("circle").has_value());
    CHECK_FALSE(fem::geom::findCommand("").has_value());

    CHECK(fem::geom::commandText(CommandName::Pline) == "PLINE");
    CHECK(fem::geom::completeCommand("pl") == std::optional<std::string_view>("PLINE"));
    CHECK(fem::geom::completeCommand("er") == std::optional<std::string_view>("ERASE"));
    CHECK(fem::geom::completeCommand("re") == std::optional<std::string_view>("REDO"));
    CHECK_FALSE(fem::geom::completeCommand("x").has_value());
}
