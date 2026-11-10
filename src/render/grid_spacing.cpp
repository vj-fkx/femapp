#include <fem/render/grid_spacing.hpp>

#include <algorithm>
#include <cmath>

namespace fem::render {

GridSpacing gridSpacing(double worldUnitsPerPixel) noexcept
{
    if (!(worldUnitsPerPixel > 0.0) || !std::isfinite(worldUnitsPerPixel)) {
        return {};
    }

    const double exponent = std::log10(worldUnitsPerPixel * kGridMinPixelsPerCell);
    const double level = std::ceil(exponent);

    GridSpacing spacing;
    spacing.minor = std::pow(10.0, level);
    spacing.major = std::pow(10.0, level + 1.0);
    spacing.minorFade = std::clamp(level - exponent, 0.0, 1.0);
    return spacing;
}

} // namespace fem::render
