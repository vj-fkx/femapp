#pragma once

#include <optional>
#include <string>

namespace fem::core {

// REQ-007-01: an isotropic linear material. Numbers carry no units: the user
// keeps them consistent (REQ-007-07). An empty optional means "not given",
// which validation tells apart from zero.
struct Material
{
    std::string name;
    std::optional<double> youngsModulus;    // E
    std::optional<double> poissonsRatio;    // nu
    std::optional<double> density;          // rho
    std::optional<double> thermalExpansion; // alpha
    std::optional<double> conductivity;     // k
    std::optional<double> specificHeat;     // c
};

// REQ-007-02: a bar section. Inertias and shape come with the beams (008).
struct Section
{
    std::string name;
    std::optional<double> area; // A
};

} // namespace fem::core
