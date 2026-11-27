#pragma once

// Feature 012 (design §4, §5.5): how a result is drawn in one frame. Every
// field here is a uniform or a choice of draw, so changing it re-sends
// nothing to the GPU (REQ-012-08, 13).

#include <cstdint>

namespace fem::render {

// REQ-012-15: the scale over time: 0 to f (a static result) or -f to f (a
// buckling mode).
enum class Animation : std::uint8_t { Off, Static, Mode };

struct ResultDisplay
{
    bool deformed = true;  // REQ-012-07: the shape moved by scale x displacements
    bool original = false; // REQ-012-07: the original shape, thin and neutral
    double scale = 1.0;    // REQ-012-06: the factor f
    bool colored = true;   // REQ-012-09: false, the 1D color of the mesh
    double min = 0.0;      // REQ-012-11: the range of the colors
    double max = 1.0;
    int bands = 0; // REQ-012-12: 0, continuous
    Animation animation = Animation::Off;
    double period = 2.0; // seconds per cycle
    bool playing = false;
};

// REQ-012-15: the factor drawn `seconds` after the animation started: the
// factor itself when it does not play; 0 to f, f (1 - cos(2 pi t / T)) / 2,
// for Static; -f to f, f sin(2 pi t / T), for Mode.
[[nodiscard]] double animatedScale(const ResultDisplay& display, double seconds) noexcept;

} // namespace fem::render
