#pragma once

namespace fem::render {

// Spacing of the adaptive grid for a given zoom level.
struct GridSpacing
{
    double minor = 1.0;     // world units between minor lines; a power of 10
    double major = 10.0;    // world units between major lines; 10 * minor
    double minorFade = 1.0; // opacity of the minor lines, in [0, 1]
};

// Minimum distance between two minor lines, in pixels.
inline constexpr double kGridMinPixelsPerCell = 12.0;

// `worldUnitsPerPixel` is the size of one pixel at the camera focus.
//
// The minor spacing is the smallest power of 10 whose cells are at least
// kGridMinPixelsPerCell wide. `minorFade` goes from 0 (cells at the minimum
// size) to 1 (cells ten times wider), so minor lines fade in while zooming in
// and have the opacity of major lines by the time they become major lines.
[[nodiscard]] GridSpacing gridSpacing(double worldUnitsPerPixel) noexcept;

} // namespace fem::render
