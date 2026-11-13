#include <fem/geom/command_input.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <locale>
#include <numbers>
#include <sstream>
#include <vector>

namespace fem::geom {

namespace {

struct CommandEntry
{
    CommandName name;
    std::string_view text;
    std::array<std::string_view, 2> aliases;
};

// REQ-005-05, in completion order.
constexpr std::array<CommandEntry, 8> kCommands = {{
    {.name = CommandName::Point, .text = "POINT", .aliases = {"PO", ""}},
    {.name = CommandName::Line, .text = "LINE", .aliases = {"L", ""}},
    {.name = CommandName::Pline, .text = "PLINE", .aliases = {"PL", ""}},
    {.name = CommandName::Move, .text = "MOVE", .aliases = {"M", ""}},
    {.name = CommandName::Copy, .text = "COPY", .aliases = {"CO", "CP"}},
    {.name = CommandName::Erase, .text = "ERASE", .aliases = {"E", ""}},
    {.name = CommandName::Undo, .text = "UNDO", .aliases = {"U", ""}},
    {.name = CommandName::Redo, .text = "REDO", .aliases = {"", ""}},
}};

std::string_view trim(std::string_view text)
{
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string upper(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return result;
}

// A whole number, with the point as decimal separator in any locale.
std::optional<double> parseNumber(std::string_view text)
{
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (stream.fail() || !std::isfinite(value)) {
        return std::nullopt;
    }
    stream >> std::ws;
    if (!stream.eof()) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(separator, start);
        if (at == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

} // namespace

std::optional<ParsedInput> parseInput(std::string_view text)
{
    text = trim(text);
    if (text.empty()) {
        return Empty{};
    }

    const bool relative = text.front() == '@';
    const std::string_view body = relative ? trim(text.substr(1)) : text;

    // Polar: [@]d<a
    if (const std::size_t angle = body.find('<'); angle != std::string_view::npos) {
        const auto distance = parseNumber(body.substr(0, angle));
        const auto degrees = parseNumber(body.substr(angle + 1));
        if (!distance || !degrees) {
            return std::nullopt;
        }
        return PolarPoint{.distance = *distance, .angleDeg = *degrees, .relative = relative};
    }

    // Cartesian: [@]x,y[,z]
    if (body.find(',') != std::string_view::npos) {
        const auto parts = split(body, ',');
        if (parts.size() < 2 || parts.size() > 3) {
            return std::nullopt;
        }
        Vec3 point = Vec3::Zero();
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto value = parseNumber(parts[i]);
            if (!value) {
                return std::nullopt;
            }
            point(static_cast<Eigen::Index>(i)) = *value;
        }
        if (relative) {
            return RelativePoint{.delta = point};
        }
        return AbsolutePoint{.point = point};
    }

    if (relative) {
        return std::nullopt;
    }
    if (const auto value = parseNumber(body)) {
        return Distance{.value = *value};
    }
    const bool letters = std::ranges::all_of(
        body, [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
    if (letters) {
        return Keyword{.text = upper(body)};
    }
    return std::nullopt;
}

std::optional<CommandName> findCommand(std::string_view text)
{
    const std::string name = upper(trim(text));
    if (name.empty()) {
        return std::nullopt;
    }
    for (const CommandEntry& entry : kCommands) {
        if (name == entry.text || name == entry.aliases[0] || name == entry.aliases[1]) {
            return entry.name;
        }
    }
    return std::nullopt;
}

std::string_view commandText(CommandName name) noexcept
{
    for (const CommandEntry& entry : kCommands) {
        if (entry.name == name) {
            return entry.text;
        }
    }
    return {};
}

std::optional<std::string_view> completeCommand(std::string_view prefix)
{
    const std::string start = upper(trim(prefix));
    if (start.empty()) {
        return std::nullopt;
    }
    for (const CommandEntry& entry : kCommands) {
        if (entry.text.starts_with(start)) {
            return entry.text;
        }
    }
    return std::nullopt;
}

std::optional<Vec3> resolvePoint(const ParsedInput& input, const std::optional<Vec3>& last,
                                 const Vec3& direction)
{
    if (const auto* absolute = std::get_if<AbsolutePoint>(&input)) {
        return absolute->point;
    }
    if (const auto* relative = std::get_if<RelativePoint>(&input)) {
        if (!last) {
            return std::nullopt;
        }
        return Vec3(*last + relative->delta);
    }
    if (const auto* polar = std::get_if<PolarPoint>(&input)) {
        if (polar->relative && !last) {
            return std::nullopt;
        }
        const Vec3 from = polar->relative ? *last : Vec3::Zero();
        const double radians = polar->angleDeg * std::numbers::pi / 180.0;
        return Vec3(from + polar->distance * Vec3(std::cos(radians), std::sin(radians), 0.0));
    }
    if (const auto* distance = std::get_if<Distance>(&input)) {
        const double length = direction.norm();
        if (!last || !(length > 0.0)) {
            return std::nullopt;
        }
        return Vec3(*last + distance->value * direction / length);
    }
    return std::nullopt;
}

} // namespace fem::geom
