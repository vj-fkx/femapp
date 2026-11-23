#include <fem/setup/convert.hpp>
#include <fem/setup/library.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The material library (design 010 §5.9). Each standard is a function that
// returns its rows; adding a standard is adding a function to the list in
// materialLibrary(), adding a grade is adding a row, adding a property is
// adding a key with its quantity (the conversion and the interface need no
// change). All values in SI: N, m, kg, s, °C.

namespace fem::setup {

namespace {

namespace q = quantity;

constexpr double kGPa = 1e9;
constexpr double kMPa = 1e6;
constexpr double kPsi = 6894.757293168361; // Pa

MaterialProperty stress(std::string_view key, double value)
{
    return {.key = std::string(key), .quantity = q::kStress, .value = value};
}

// --- Steel ------------------------------------------------------------------

struct SteelGrade
{
    const char* grade;
    double fy; // MPa
    double fu; // MPa
};

struct SteelStandard
{
    const char* standard;
    double e;     // Pa
    double g;     // Pa, as the standard gives it
    double alpha; // 1/°C
};

// A row of steel: its grade, fy and fu in MPa.
SteelGrade steel(const char* grade, double fy, double fu)
{
    return {.grade = grade, .fy = fy, .fu = fu};
}

void addSteels(std::vector<LibraryMaterial>& out, const SteelStandard& standard,
               std::initializer_list<SteelGrade> grades)
{
    for (const SteelGrade& g : grades) {
        out.push_back({.standard = standard.standard,
                       .family = MaterialFamily::Steel,
                       .grade = g.grade,
                       .material = {.name = {},
                                    .youngsModulus = standard.e,
                                    .poissonsRatio = 0.3,
                                    .density = 7850.0,
                                    .thermalExpansion = standard.alpha},
                       .properties = {stress(property::kYield, g.fy * kMPa),
                                      stress(property::kUltimate, g.fu * kMPa),
                                      stress(property::kShearModulus, standard.g)}});
    }
}

// EN 1993-1-1, 3.2.6 and table 3.1 (EN 10025-2 and -4, t <= 40 mm).
void eurocodeSteels(std::vector<LibraryMaterial>& out)
{
    addSteels(out, {.standard = "EN 1993-1-1", .e = 210 * kGPa, .g = 81 * kGPa, .alpha = 12e-6},
              {steel("S235", 235, 360), steel("S275", 275, 430), steel("S355", 355, 490),
               steel("S460", 460, 540)});
}

// AISC 360 (E = 29 000 ksi, G = 11 200 ksi), ASTM grades.
void aiscSteels(std::vector<LibraryMaterial>& out)
{
    addSteels(out, {.standard = "AISC 360", .e = 200 * kGPa, .g = 77.2 * kGPa, .alpha = 11.7e-6},
              {steel("A36", 250, 400), steel("A572 Gr 50", 345, 450), steel("A992", 345, 450)});
}

// ABNT NBR 8800, 4.5.2.9 and annex A.
void nbr8800Steels(std::vector<LibraryMaterial>& out)
{
    addSteels(out, {.standard = "ABNT NBR 8800", .e = 200 * kGPa, .g = 77 * kGPa, .alpha = 12e-6},
              {steel("MR250", 250, 400), steel("A572 Gr 50", 345, 450)});
}

// --- Concrete ---------------------------------------------------------------

LibraryMaterial concrete(const char* standard, std::string grade, double fck, double e,
                         double density, double alpha)
{
    return {.standard = standard,
            .family = MaterialFamily::Concrete,
            .grade = std::move(grade),
            .material = {.name = {},
                         .youngsModulus = e,
                         .poissonsRatio = 0.2,
                         .density = density,
                         .thermalExpansion = alpha},
            .properties = {stress(property::kCompressive, fck)}};
}

// EN 1992-1-1, table 3.1: Ecm = 22 [(fck + 8) / 10]^0.3 GPa (quartzite
// aggregates); reinforced concrete 25 kN/m³ (EN 1991-1-1) as 2500 kg/m³.
void eurocodeConcretes(std::vector<LibraryMaterial>& out)
{
    const std::initializer_list<std::pair<const char*, double>> grades{
        {"C20/25", 20}, {"C25/30", 25}, {"C30/37", 30}, {"C35/45", 35},
        {"C40/50", 40}, {"C45/55", 45}, {"C50/60", 50}};
    for (const auto& [grade, fck] : grades) {
        out.push_back(concrete("EN 1992-1-1", grade, fck * kMPa,
                               22.0 * std::pow((fck + 8.0) / 10.0, 0.3) * kGPa, 2500.0, 10e-6));
    }
}

// ACI 318, 19.2.2.1: Ec = 4700 sqrt(f'c) MPa (normalweight concrete).
void aciConcretes(std::vector<LibraryMaterial>& out)
{
    for (const int psi : {3000, 4000, 5000, 6000}) {
        const double fc = psi * kPsi;
        out.push_back(concrete("ACI 318", std::to_string(psi) + " psi", fc,
                               4700.0 * std::sqrt(fc / kMPa) * kMPa, 2400.0, 9.9e-6));
    }
}

// ABNT NBR 6118, 8.2.8: Eci = alpha_E 5600 sqrt(fck) MPa (alpha_E = 1,
// granite and gneiss), Ecs = alpha_i Eci with alpha_i = 0.8 + 0.2 fck / 80
// <= 1; reinforced concrete 2500 kg/m³ (8.2.2).
void nbr6118Concretes(std::vector<LibraryMaterial>& out)
{
    for (int fck = 20; fck <= 50; fck += 5) {
        const double alphaI = std::min(1.0, 0.8 + 0.2 * fck / 80.0);
        out.push_back(concrete("ABNT NBR 6118", "C" + std::to_string(fck), fck * kMPa,
                               alphaI * 5600.0 * std::sqrt(static_cast<double>(fck)) * kMPa, 2500.0,
                               10e-6));
    }
}

// --- Aluminium and timber ---------------------------------------------------

// EN 1999-1-1, 3.2.5.
void eurocodeAluminium(std::vector<LibraryMaterial>& out)
{
    out.push_back({.standard = "EN 1999-1-1",
                   .family = MaterialFamily::Aluminium,
                   .grade = "Aluminium",
                   .material = {.name = {},
                                .youngsModulus = 70 * kGPa,
                                .poissonsRatio = 0.3,
                                .density = 2700.0,
                                .thermalExpansion = 23e-6},
                   .properties = {}});
}

// EN 338, table 1: E0,mean, G_mean, rho_mean, fm,k. Timber is not
// isotropic: nu is left out (design §5.9).
void en338Timber(std::vector<LibraryMaterial>& out)
{
    out.push_back({.standard = "EN 338",
                   .family = MaterialFamily::Timber,
                   .grade = "C24",
                   .material = {.name = {}, .youngsModulus = 11 * kGPa, .density = 420.0},
                   .properties = {stress(property::kBending, 24 * kMPa),
                                  stress(property::kShearModulus, 0.69 * kGPa)}});
}

std::vector<LibraryMaterial> build()
{
    std::vector<LibraryMaterial> out;
    // The standards, in the order of the table of design §5.9.
    for (const auto standard : {eurocodeSteels, aiscSteels, nbr8800Steels, eurocodeConcretes,
                                aciConcretes, nbr6118Concretes, eurocodeAluminium, en338Timber}) {
        standard(out);
    }
    return out;
}

} // namespace

const std::vector<LibraryMaterial>& materialLibrary()
{
    static const std::vector<LibraryMaterial> library = build();
    return library;
}

MaterialItem fromLibrary(const LibraryMaterial& material, UnitSystem to)
{
    Item item = MaterialItem{.id = {},
                             .data = material.material,
                             .standard = material.standard,
                             .grade = material.grade,
                             .properties = material.properties};
    std::get<MaterialItem>(item).data.name = material.grade + " (" + material.standard + ")";
    convertItem(item, UnitSystem::NewtonMetre, to);
    return std::get<MaterialItem>(item);
}

} // namespace fem::setup
