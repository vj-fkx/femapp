#pragma once

// Feature 010: the unit systems of the model document (design 010 §5.1).

#include <array>
#include <cstdint>
#include <string>

namespace fem::setup {

// REQ-010-02: the coherent systems offered.
enum class UnitSystem : std::uint8_t {
    NewtonMetre,             // N, m, kg, s, °C
    KilonewtonMetre,         // kN, m, t, s, °C
    NewtonMillimetre,        // N, mm, t, s, °C
    KilogramForceCentimetre, // kgf, cm, kgf s²/cm, s, °C
    PoundInch                // lbf, in, lbf s²/in, s, °F
};

inline constexpr std::array<UnitSystem, 5> kUnitSystems{
    UnitSystem::NewtonMetre, UnitSystem::KilonewtonMetre, UnitSystem::NewtonMillimetre,
    UnitSystem::KilogramForceCentimetre, UnitSystem::PoundInch};

// A quantity as exponents of force, length, time and temperature (a
// difference of temperature); mass is F T² / L in a coherent system.
struct Quantity
{
    int force = 0;
    int length = 0;
    int time = 0;
    int temperature = 0;

    friend bool operator==(const Quantity&, const Quantity&) = default;
};

// The quantities of the document.
namespace quantity {
inline constexpr Quantity kNone{};
inline constexpr Quantity kLength{.length = 1};
inline constexpr Quantity kArea{.length = 2};
inline constexpr Quantity kInertia{.length = 4};
inline constexpr Quantity kWarping{.length = 6};
inline constexpr Quantity kForce{.force = 1};
inline constexpr Quantity kMoment{.force = 1, .length = 1};
inline constexpr Quantity kBimoment{.force = 1, .length = 2};
inline constexpr Quantity kStress{.force = 1, .length = -2};
inline constexpr Quantity kForcePerLength{.force = 1, .length = -1};
inline constexpr Quantity kDensity{.force = 1, .length = -4, .time = 2};
inline constexpr Quantity kAcceleration{.length = 1, .time = -2};
inline constexpr Quantity kTemperature{.temperature = 1};
inline constexpr Quantity kPerTemperature{.temperature = -1};
inline constexpr Quantity kGradient{.length = -1, .temperature = 1};
inline constexpr Quantity kConductivity{.force = 1, .time = -1, .temperature = -1};
inline constexpr Quantity kSpecificHeat{.length = 2, .time = -2, .temperature = -1};
inline constexpr Quantity kSpringPerLength{.force = 1, .length = -2};
inline constexpr Quantity kRotationalSpring{.force = 1, .length = 1};
} // namespace quantity

// The names of the base units of a system (UTF-8).
struct UnitNames
{
    std::string force;
    std::string length;
    std::string mass;
    std::string time;
    std::string temperature;
};

[[nodiscard]] UnitNames unitNames(UnitSystem system);

// REQ-010-02: the unit of a quantity in a system, "kN/m²", "t/m³", "N·mm";
// empty for a number without unit.
[[nodiscard]] std::string unitLabel(UnitSystem system, Quantity quantity);

// The factor that turns a value of `quantity` in `from` into `to`.
[[nodiscard]] double conversionFactor(UnitSystem from, UnitSystem to, Quantity quantity);

} // namespace fem::setup
