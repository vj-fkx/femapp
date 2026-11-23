#include <fem/setup/units.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>

// Feature 010, T03 (design §5.1): REQ-010-02, NFR-010-02.

namespace {

using fem::setup::conversionFactor;
using fem::setup::Quantity;
using fem::setup::UnitSystem;
namespace q = fem::setup::quantity;

bool close(double a, double b, double tol = 1e-12)
{
    return std::abs(a - b) <= tol * std::max(std::abs(a), std::abs(b));
}

constexpr std::array<Quantity, 19> kAll{q::kNone,
                                        q::kLength,
                                        q::kArea,
                                        q::kInertia,
                                        q::kWarping,
                                        q::kForce,
                                        q::kMoment,
                                        q::kBimoment,
                                        q::kStress,
                                        q::kForcePerLength,
                                        q::kDensity,
                                        q::kAcceleration,
                                        q::kTemperature,
                                        q::kPerTemperature,
                                        q::kGradient,
                                        q::kConductivity,
                                        q::kSpecificHeat,
                                        q::kSpringPerLength,
                                        q::kRotationalSpring};

} // namespace

TEST_CASE("the unit systems offered", "[setup][units]")
{
    CHECK(fem::setup::kUnitSystems.size() == 5);
    CHECK(fem::setup::kUnitSystems.front() == UnitSystem::NewtonMetre);
}

TEST_CASE("the factors of the systems", "[setup][units]")
{
    // To SI.
    CHECK(close(conversionFactor(UnitSystem::KilonewtonMetre, UnitSystem::NewtonMetre, q::kForce),
                1e3));
    CHECK(close(conversionFactor(UnitSystem::NewtonMillimetre, UnitSystem::NewtonMetre, q::kLength),
                1e-3));
    CHECK(close(
        conversionFactor(UnitSystem::KilogramForceCentimetre, UnitSystem::NewtonMetre, q::kForce),
        9.80665));
    CHECK(close(conversionFactor(UnitSystem::PoundInch, UnitSystem::NewtonMetre, q::kForce),
                4.4482216152605));
    CHECK(close(conversionFactor(UnitSystem::PoundInch, UnitSystem::NewtonMetre, q::kLength),
                0.0254));
    // Temperature differences: a degree Fahrenheit is 5/9 of a kelvin.
    CHECK(close(conversionFactor(UnitSystem::PoundInch, UnitSystem::NewtonMetre, q::kTemperature),
                5.0 / 9.0));
    CHECK(
        close(conversionFactor(UnitSystem::PoundInch, UnitSystem::NewtonMetre, q::kPerTemperature),
              9.0 / 5.0));
    // Derived: 1 N/mm^2 = 1 MPa; 1 kN m = 1e3 N m.
    CHECK(close(conversionFactor(UnitSystem::NewtonMillimetre, UnitSystem::NewtonMetre, q::kStress),
                1e6));
    CHECK(close(conversionFactor(UnitSystem::KilonewtonMetre, UnitSystem::NewtonMetre, q::kMoment),
                1e3));
    CHECK(close(conversionFactor(UnitSystem::NewtonMetre, UnitSystem::NewtonMetre, q::kWarping),
                1.0));
}

// NFR-010-02: there and back between any two systems.
TEST_CASE("conversions go there and back", "[setup][units]")
{
    for (const UnitSystem from : fem::setup::kUnitSystems) {
        for (const UnitSystem to : fem::setup::kUnitSystems) {
            for (const Quantity quantity : kAll) {
                const double value = 1234.5678;
                const double there = value * conversionFactor(from, to, quantity);
                const double back = there * conversionFactor(to, from, quantity);
                CHECK(close(back, value));
            }
        }
    }
}

// REQ-010-02: the systems are coherent, so formulas give the right unit.
TEST_CASE("the systems are coherent", "[setup][units]")
{
    // A steel bar: E = 210 GPa, A = 0.01 m^2, L = 2 m: E A / L = 1.05e9 N/m.
    const double e = 2.1e11;
    const double area = 0.01;
    const double length = 2.0;
    for (const UnitSystem system : fem::setup::kUnitSystems) {
        INFO(static_cast<int>(system));
        const auto in = [&](double si, Quantity quantity) {
            return si * conversionFactor(UnitSystem::NewtonMetre, system, quantity);
        };
        const double stiffness = in(e, q::kStress) * in(area, q::kArea) / in(length, q::kLength);
        CHECK(
            close(stiffness * conversionFactor(system, UnitSystem::NewtonMetre, q::kForcePerLength),
                  1.05e9));
        // Steel weighs rho g = 7850 kg/m^3 x 9.80665 m/s^2 per volume.
        const double weight = in(7850.0, q::kDensity) * in(9.80665, q::kAcceleration);
        const Quantity perVolume{.force = 1, .length = -3, .time = 0, .temperature = 0};
        CHECK(close(weight * conversionFactor(system, UnitSystem::NewtonMetre, perVolume),
                    7850.0 * 9.80665));
    }
    // The coherent masses: 7.85 t/m^3; a kilogram-force per cm s^2 is
    // 980.665 kg.
    CHECK(close(7850.0 * conversionFactor(UnitSystem::NewtonMetre, UnitSystem::KilonewtonMetre,
                                          q::kDensity),
                7.85));
    CHECK(close(7850.0 * conversionFactor(UnitSystem::NewtonMetre, UnitSystem::NewtonMillimetre,
                                          q::kDensity),
                7.85e-9));
}

TEST_CASE("the names and labels of the units", "[setup][units]")
{
    const auto kn = fem::setup::unitNames(UnitSystem::KilonewtonMetre);
    CHECK(kn.force == "kN");
    CHECK(kn.length == "m");
    CHECK(kn.mass == "t");
    CHECK(kn.time == "s");
    CHECK(kn.temperature == "°C");
    CHECK(fem::setup::unitNames(UnitSystem::PoundInch).temperature == "°F");
    CHECK(fem::setup::unitNames(UnitSystem::KilogramForceCentimetre).mass == "kgf·s²/cm");

    using fem::setup::unitLabel;
    CHECK(unitLabel(UnitSystem::KilonewtonMetre, q::kStress) == "kN/m²");
    CHECK(unitLabel(UnitSystem::KilonewtonMetre, q::kDensity) == "t/m³");
    CHECK(unitLabel(UnitSystem::NewtonMillimetre, q::kMoment) == "N·mm");
    CHECK(unitLabel(UnitSystem::NewtonMetre, q::kInertia) == "m⁴");
    CHECK(unitLabel(UnitSystem::NewtonMetre, q::kWarping) == "m⁶");
    CHECK(unitLabel(UnitSystem::NewtonMetre, q::kAcceleration) == "m/s²");
    CHECK(unitLabel(UnitSystem::PoundInch, q::kPerTemperature) == "1/°F");
    CHECK(unitLabel(UnitSystem::NewtonMetre, q::kGradient) == "°C/m");
    CHECK(unitLabel(UnitSystem::KilonewtonMetre, q::kForcePerLength) == "kN/m");
    CHECK(unitLabel(UnitSystem::NewtonMetre, q::kNone).empty());
}
