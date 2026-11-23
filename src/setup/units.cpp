#include <fem/setup/units.hpp>

#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::setup {

namespace {

// The base units of a system in SI (design §5.1).
struct Base
{
    double force = 1.0;       // N
    double length = 1.0;      // m
    double temperature = 1.0; // K per degree (differences)
};

Base baseOf(UnitSystem system)
{
    switch (system) {
    case UnitSystem::NewtonMetre:
        return {.force = 1.0, .length = 1.0, .temperature = 1.0};
    case UnitSystem::KilonewtonMetre:
        return {.force = 1e3, .length = 1.0, .temperature = 1.0};
    case UnitSystem::NewtonMillimetre:
        return {.force = 1.0, .length = 1e-3, .temperature = 1.0};
    case UnitSystem::KilogramForceCentimetre:
        return {.force = 9.80665, .length = 1e-2, .temperature = 1.0};
    case UnitSystem::PoundInch:
        return {.force = 4.4482216152605, .length = 0.0254, .temperature = 5.0 / 9.0};
    }
    return {};
}

// A value of `quantity` in `system`, in SI.
double toSi(UnitSystem system, Quantity quantity)
{
    const Base b = baseOf(system);
    return std::pow(b.force, quantity.force) * std::pow(b.length, quantity.length) *
           std::pow(b.temperature, quantity.temperature);
}

std::string_view superscript(int power)
{
    switch (power) {
    case 2:
        return "²";
    case 3:
        return "³";
    case 4:
        return "⁴";
    case 5:
        return "⁵";
    case 6:
        return "⁶";
    default:
        return "";
    }
}

// "m²", or "m" for the first power.
std::string power(const std::string& unit, int exponent)
{
    std::string text = unit;
    if (exponent > 1) {
        text += superscript(exponent);
    }
    return text;
}

std::string joined(const std::vector<std::string>& parts)
{
    std::string text;
    for (const std::string& part : parts) {
        if (!text.empty()) {
            text += "·";
        }
        text += part;
    }
    return text;
}

} // namespace

UnitNames unitNames(UnitSystem system)
{
    switch (system) {
    case UnitSystem::NewtonMetre:
        return {.force = "N", .length = "m", .mass = "kg", .time = "s", .temperature = "°C"};
    case UnitSystem::KilonewtonMetre:
        return {.force = "kN", .length = "m", .mass = "t", .time = "s", .temperature = "°C"};
    case UnitSystem::NewtonMillimetre:
        return {.force = "N", .length = "mm", .mass = "t", .time = "s", .temperature = "°C"};
    case UnitSystem::KilogramForceCentimetre:
        return {
            .force = "kgf", .length = "cm", .mass = "kgf·s²/cm", .time = "s", .temperature = "°C"};
    case UnitSystem::PoundInch:
        return {
            .force = "lbf", .length = "in", .mass = "lbf·s²/in", .time = "s", .temperature = "°F"};
    }
    return {};
}

std::string unitLabel(UnitSystem system, Quantity quantity)
{
    const UnitNames names = unitNames(system);
    int force = quantity.force;
    int length = quantity.length;
    int time = quantity.time;
    int mass = 0;
    // A mass of a system with its own name (kg, t): F T² / L. The systems
    // whose mass is a compound unit keep force and time.
    const bool namedMass =
        system != UnitSystem::KilogramForceCentimetre && system != UnitSystem::PoundInch;
    if (namedMass && force > 0 && time == 2 * force) {
        mass = force;
        length += force;
        time = 0;
        force = 0;
    }
    std::vector<std::string> above;
    std::vector<std::string> below;
    const auto put = [&](const std::string& unit, int exponent) {
        if (exponent > 0) {
            above.push_back(power(unit, exponent));
        } else if (exponent < 0) {
            below.push_back(power(unit, -exponent));
        }
    };
    put(names.mass, mass);
    put(names.force, force);
    put(names.length, length);
    put(names.time, time);
    put(names.temperature, quantity.temperature);
    if (above.empty() && below.empty()) {
        return {};
    }
    std::string text = above.empty() ? std::string("1") : joined(above);
    if (!below.empty()) {
        const std::string under = joined(below);
        text += below.size() > 1 ? "/(" + under + ")" : "/" + under;
    }
    return text;
}

double conversionFactor(UnitSystem from, UnitSystem to, Quantity quantity)
{
    return toSi(from, quantity) / toSi(to, quantity);
}

} // namespace fem::setup
