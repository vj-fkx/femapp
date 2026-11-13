#pragma once

#include <fem/geom/drawing.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace fem::geom {

// REQ-005-05
enum class CommandName : std::uint8_t { Point, Line, Pline, Move, Copy, Erase, Undo, Redo };

struct AbsolutePoint // x,y[,z]
{
    Vec3 point = Vec3::Zero();
};
struct RelativePoint // @dx,dy[,dz]
{
    Vec3 delta = Vec3::Zero();
};
struct PolarPoint // [@]d<a, angle in degrees in the XY plane from +X
{
    double distance = 0.0;
    double angleDeg = 0.0;
    bool relative = false;
};
struct Distance // a single number: direct distance entry
{
    double value = 0.0;
};
struct Keyword // a command name or an option
{
    std::string text;
};
struct Empty // Enter or Space on an empty line
{
};

using ParsedInput =
    std::variant<Empty, AbsolutePoint, RelativePoint, PolarPoint, Distance, Keyword>;

// REQ-005-08, 10 to 13: text typed on the command line, or std::nullopt when
// it is malformed. The decimal separator is the point, whatever the locale.
[[nodiscard]] std::optional<ParsedInput> parseInput(std::string_view text);

// REQ-005-05: a command name or alias, in any case.
[[nodiscard]] std::optional<CommandName> findCommand(std::string_view text);
// The full name of a command, upper case.
[[nodiscard]] std::string_view commandText(CommandName name) noexcept;
// Completion for Tab: the first command name starting with `prefix`.
[[nodiscard]] std::optional<std::string_view> completeCommand(std::string_view prefix);

// Turns a parsed point, or a distance along `direction`, into a point;
// std::nullopt when it needs a previous point that does not exist (or a
// direction of zero length).
[[nodiscard]] std::optional<Vec3>
resolvePoint(const ParsedInput& input, const std::optional<Vec3>& last, const Vec3& direction);

} // namespace fem::geom
