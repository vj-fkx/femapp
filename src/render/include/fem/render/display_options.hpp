#pragma once

#include <array>
#include <cstdint>

namespace fem::render {

// REQ-004-11
enum class DisplayMode : std::uint8_t { ShadedWithEdges, Shaded, Wireframe };

// REQ-004-14
enum class ColorMode : std::uint8_t { ByDimension, ByGroup };

// REQ-004-19
enum class SelectionMode : std::uint8_t { Elements, Nodes };

struct DisplayOptions
{
    DisplayMode mode = DisplayMode::ShadedWithEdges;
    ColorMode color = ColorMode::ByDimension;
    // REQ-004-16: visibility of the elements of each dimension, 0D to 3D.
    std::array<bool, 4> dimensionVisible = {true, true, true, true};

    [[nodiscard]] bool isVisible(int dimension) const noexcept
    {
        return dimension >= 0 && dimension < 4 &&
               dimensionVisible[static_cast<std::size_t>(dimension)];
    }
};

} // namespace fem::render
