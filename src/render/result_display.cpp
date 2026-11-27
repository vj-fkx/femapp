#include <fem/render/result_display.hpp>

#include <cmath>
#include <numbers>

namespace fem::render {

double animatedScale(const ResultDisplay& display, double seconds) noexcept
{
    if (!display.playing || display.animation == Animation::Off || !(display.period > 0.0)) {
        return display.scale;
    }
    const double phase = 2.0 * std::numbers::pi * seconds / display.period;
    if (display.animation == Animation::Static) {
        return display.scale * (1.0 - std::cos(phase)) / 2.0;
    }
    return display.scale * std::sin(phase);
}

} // namespace fem::render
