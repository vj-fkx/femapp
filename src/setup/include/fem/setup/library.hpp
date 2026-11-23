#pragma once

// Feature 010: the material library by standard (design 010 §5.9,
// REQ-010-04). A data table in SI: a standard is a function returning its
// rows, a grade is a row, an extra property is a key with its quantity.

#include <fem/core/material.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fem::setup {

enum class MaterialFamily : std::uint8_t { Steel, Concrete, Aluminium, Timber };

// The keys of the extra properties in the library; others may be added.
namespace property {
inline constexpr std::string_view kYield = "fy";        // yield strength
inline constexpr std::string_view kUltimate = "fu";     // ultimate tensile strength
inline constexpr std::string_view kCompressive = "fck"; // characteristic compressive strength
inline constexpr std::string_view kBending = "fmk";     // characteristic bending strength
inline constexpr std::string_view kShearModulus = "G";  // shear modulus given by the standard
} // namespace property

// One material of the library, in SI.
struct LibraryMaterial
{
    std::string standard; // "EN 1993-1-1"
    MaterialFamily family = MaterialFamily::Steel;
    std::string grade; // "S355"
    core::Material material;
    std::vector<MaterialProperty> properties;
};

// Every material of every standard, in the order of the table of design
// §5.9.
[[nodiscard]] const std::vector<LibraryMaterial>& materialLibrary();

// The document item of a library material, named "<grade> (<standard>)",
// in the unit system `to` (its id is left for addItem).
[[nodiscard]] MaterialItem fromLibrary(const LibraryMaterial& material, UnitSystem to);

} // namespace fem::setup
