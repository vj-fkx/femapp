#pragma once

// Section moved to section.hpp in feature 008; included here so that code of
// feature 007 keeps compiling.
#include <fem/core/section.hpp>

#include <optional>
#include <string>

namespace fem::core {

// REQ-007-01: an isotropic linear material. Numbers carry no units: the user
// keeps them consistent (REQ-007-07). An empty optional means "not given",
// which validation tells apart from zero.
struct Material
{
    std::string name;
    std::optional<double> youngsModulus = std::nullopt;    // E
    std::optional<double> poissonsRatio = std::nullopt;    // nu
    std::optional<double> density = std::nullopt;          // rho
    std::optional<double> thermalExpansion = std::nullopt; // alpha
    std::optional<double> conductivity = std::nullopt;     // k
    std::optional<double> specificHeat = std::nullopt;     // c
};

} // namespace fem::core
