#include <fem/setup/convert.hpp>
#include <fem/setup/library.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Feature 010, T07 (design §5.9): REQ-010-04. The values of the table of
// the design, written here again on their own.

namespace {

namespace s = fem::setup;
using s::UnitSystem;

constexpr double kGPa = 1e9;
constexpr double kMPa = 1e6;

bool close(double a, double b, double tol = 1e-12)
{
    return std::abs(a - b) <= tol * std::max(std::abs(a), std::abs(b));
}

const s::LibraryMaterial& find(const std::string& standard, const std::string& grade)
{
    for (const s::LibraryMaterial& m : s::materialLibrary()) {
        if (m.standard == standard && m.grade == grade) {
            return m;
        }
    }
    FAIL("no " << grade << " in " << standard);
    throw std::logic_error("unreachable");
}

// For two literals: the strings live in this call, not in the caller's
// statement (as g++ -Wdangling-reference fears for a temporary there).
const s::LibraryMaterial& find(const char* standard, const char* grade)
{
    return find(std::string(standard), std::string(grade));
}

std::optional<double> propertyOf(const std::vector<s::MaterialProperty>& properties,
                                 std::string_view key)
{
    for (const s::MaterialProperty& p : properties) {
        if (p.key == key) {
            return p.value;
        }
    }
    return std::nullopt;
}

struct Steel
{
    std::string grade;
    double fy = 0.0;
    double fu = 0.0;
};

Steel steel(const char* grade, double fy, double fu)
{
    return {.grade = grade, .fy = fy, .fu = fu};
}

void checkSteels(const std::string& standard, const std::vector<Steel>& steels, double e,
                 double alpha, double g)
{
    for (const Steel& steel : steels) {
        INFO(standard << " " << steel.grade);
        const s::LibraryMaterial& m = find(standard, steel.grade);
        CHECK(m.family == s::MaterialFamily::Steel);
        CHECK(close(m.material.youngsModulus.value_or(0.0), e));
        CHECK(close(m.material.poissonsRatio.value_or(0.0), 0.3));
        CHECK(close(m.material.density.value_or(0.0), 7850.0));
        CHECK(close(m.material.thermalExpansion.value_or(0.0), alpha));
        CHECK_FALSE(m.material.conductivity.has_value());
        CHECK_FALSE(m.material.specificHeat.has_value());
        CHECK(close(propertyOf(m.properties, s::property::kYield).value_or(0.0), steel.fy * kMPa));
        CHECK(
            close(propertyOf(m.properties, s::property::kUltimate).value_or(0.0), steel.fu * kMPa));
        CHECK(close(propertyOf(m.properties, s::property::kShearModulus).value_or(0.0), g));
    }
}

} // namespace

TEST_CASE("the steels of the library", "[setup][library]")
{
    checkSteels("EN 1993-1-1",
                {steel("S235", 235, 360), steel("S275", 275, 430), steel("S355", 355, 490),
                 steel("S460", 460, 540)},
                210 * kGPa, 12e-6, 81 * kGPa);
    checkSteels("AISC 360",
                {steel("A36", 250, 400), steel("A572 Gr 50", 345, 450), steel("A992", 345, 450)},
                200 * kGPa, 11.7e-6, 77.2 * kGPa);
    checkSteels("ABNT NBR 8800", {steel("MR250", 250, 400), steel("A572 Gr 50", 345, 450)},
                200 * kGPa, 12e-6, 77 * kGPa);
}

TEST_CASE("the concretes of the library follow their standards", "[setup][library]")
{
    const auto checkConcrete = [](const s::LibraryMaterial& m, double fck, double e, double rho,
                                  double alpha) {
        CHECK(m.family == s::MaterialFamily::Concrete);
        CHECK(close(m.material.youngsModulus.value_or(0.0), e));
        CHECK(close(m.material.poissonsRatio.value_or(0.0), 0.2));
        CHECK(close(m.material.density.value_or(0.0), rho));
        CHECK(close(m.material.thermalExpansion.value_or(0.0), alpha));
        CHECK(close(propertyOf(m.properties, s::property::kCompressive).value_or(0.0), fck * kMPa));
    };
    // EN 1992-1-1, table 3.1: Ecm = 22 [(fck + 8) / 10]^0.3 GPa.
    const std::vector<std::pair<std::string, double>> eurocode{
        {"C20/25", 20}, {"C25/30", 25}, {"C30/37", 30}, {"C35/45", 35},
        {"C40/50", 40}, {"C45/55", 45}, {"C50/60", 50}};
    for (const auto& [grade, fck] : eurocode) {
        INFO(grade);
        checkConcrete(find("EN 1992-1-1", grade), fck,
                      22.0 * std::pow((fck + 8.0) / 10.0, 0.3) * kGPa, 2500.0, 10e-6);
    }
    CHECK(
        close(find("EN 1992-1-1", "C30/37").material.youngsModulus.value_or(0.0), 32.8366e9, 1e-5));
    // ACI 318: Ec = 4700 sqrt(f'c) MPa, f'c in psi.
    for (const double psi : {3000.0, 4000.0, 5000.0, 6000.0}) {
        const double fc = psi * 6.894757293168361e-3; // MPa
        const std::string grade = std::to_string(static_cast<int>(psi)) + " psi";
        INFO(grade);
        checkConcrete(find("ACI 318", grade), fc, 4700.0 * std::sqrt(fc) * kMPa, 2400.0, 9.9e-6);
    }
    // ABNT NBR 6118: Ecs = alpha_i 5600 sqrt(fck) MPa, alpha_E = 1,
    // alpha_i = 0.8 + 0.2 fck / 80 <= 1.
    for (int fck = 20; fck <= 50; fck += 5) {
        const std::string grade = "C" + std::to_string(fck);
        INFO(grade);
        const double alphaI = std::min(1.0, 0.8 + 0.2 * fck / 80.0);
        checkConcrete(find("ABNT NBR 6118", grade), fck, alphaI * 5600.0 * std::sqrt(fck) * kMPa,
                      2500.0, 10e-6);
    }
}

TEST_CASE("aluminium and timber, and the size of the library", "[setup][library]")
{
    const s::LibraryMaterial& aluminium = find("EN 1999-1-1", "Aluminium");
    CHECK(aluminium.family == s::MaterialFamily::Aluminium);
    CHECK(close(aluminium.material.youngsModulus.value_or(0.0), 70 * kGPa));
    CHECK(close(aluminium.material.poissonsRatio.value_or(0.0), 0.3));
    CHECK(close(aluminium.material.density.value_or(0.0), 2700.0));
    CHECK(close(aluminium.material.thermalExpansion.value_or(0.0), 23e-6));

    const s::LibraryMaterial& timber = find("EN 338", "C24");
    CHECK(timber.family == s::MaterialFamily::Timber);
    CHECK(close(timber.material.youngsModulus.value_or(0.0), 11 * kGPa));
    CHECK_FALSE(timber.material.poissonsRatio.has_value()); // not isotropic
    CHECK(close(timber.material.density.value_or(0.0), 420.0));
    CHECK(close(propertyOf(timber.properties, s::property::kBending).value_or(0.0), 24 * kMPa));
    CHECK(close(propertyOf(timber.properties, s::property::kShearModulus).value_or(0.0),
                0.69 * kGPa));

    // 4 + 3 + 2 steels, 7 + 4 + 7 concretes, aluminium, timber.
    CHECK(s::materialLibrary().size() == 29);
    // Grades are unique within a standard.
    std::map<std::pair<std::string, std::string>, int> seen;
    for (const s::LibraryMaterial& m : s::materialLibrary()) {
        CHECK(++seen[{m.standard, m.grade}] == 1);
    }
}

TEST_CASE("a library material in the units of the document", "[setup][library]")
{
    const s::MaterialItem item =
        s::fromLibrary(find("EN 1993-1-1", "S355"), UnitSystem::NewtonMillimetre);
    CHECK(item.data.name == "S355 (EN 1993-1-1)");
    CHECK(item.standard == "EN 1993-1-1");
    CHECK(item.grade == "S355");
    CHECK(close(item.data.youngsModulus.value_or(0.0), 210000.0));
    CHECK(close(item.data.density.value_or(0.0), 7.85e-9));
    CHECK(close(item.data.thermalExpansion.value_or(0.0), 12e-6));
    CHECK(close(propertyOf(item.properties, s::property::kYield).value_or(0.0), 355.0));

    // The extra properties go with the units, there and back, whatever the
    // key.
    s::Item converted = item;
    std::get<s::MaterialItem>(converted).properties.push_back(
        {.key = "my own", .quantity = s::quantity::kStress, .value = 100.0});
    s::convertItem(converted, UnitSystem::NewtonMillimetre, UnitSystem::PoundInch);
    const auto& there = std::get<s::MaterialItem>(converted).properties;
    CHECK(close(propertyOf(there, "my own").value_or(0.0), 100.0e6 / 6894.757293168361));
    s::convertItem(converted, UnitSystem::PoundInch, UnitSystem::NewtonMillimetre);
    const auto& back = std::get<s::MaterialItem>(converted).properties;
    CHECK(close(propertyOf(back, s::property::kYield).value_or(0.0), 355.0));
    CHECK(close(propertyOf(back, "my own").value_or(0.0), 100.0));
}
