#include <fem/render/colormap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fem::render {

namespace {

struct Stop
{
    double r;
    double g;
    double b;
};

constexpr std::array<Stop, 5> kStops = {{
    {.r = 0.0, .g = 0.0, .b = 255.0},   // blue
    {.r = 0.0, .g = 255.0, .b = 255.0}, // cyan
    {.r = 0.0, .g = 255.0, .b = 0.0},   // green
    {.r = 255.0, .g = 255.0, .b = 0.0}, // yellow
    {.r = 255.0, .g = 0.0, .b = 0.0},   // red
}};

Rgba pack(double r, double g, double b)
{
    const auto byte = [](double v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 255.0)));
    };
    return byte(r) | (byte(g) << 8U) | (byte(b) << 16U) | (0xFFU << 24U);
}

// Where a value lies in [min, max], clamped to [0, 1]; 0 when the range is
// empty.
double fraction(double value, double min, double max)
{
    if (!(max > min)) {
        return 0.0;
    }
    return std::clamp((value - min) / (max - min), 0.0, 1.0);
}

} // namespace

Rgba colormapColor(double t) noexcept
{
    const double at = std::clamp(std::isfinite(t) ? t : 0.0, 0.0, 1.0) * (kStops.size() - 1);
    const auto i = std::min(static_cast<std::size_t>(at), kStops.size() - 2);
    const double s = at - static_cast<double>(i);
    const Stop& a = kStops.at(i);
    const Stop& b = kStops.at(i + 1);
    return pack(a.r + s * (b.r - a.r), a.g + s * (b.g - a.g), a.b + s * (b.b - a.b));
}

int bandOf(double value, double min, double max, int bands) noexcept
{
    if (bands < 1 || !(max > min)) {
        return 0;
    }
    const double scaled = std::floor((value - min) / (max - min) * bands);
    if (!(scaled >= 0.0)) {
        return 0;
    }
    return static_cast<int>(std::min(scaled, static_cast<double>(bands - 1)));
}

Rgba valueColor(double value, double min, double max, int bands) noexcept
{
    if (!std::isfinite(value)) {
        return noValueColor();
    }
    if (bands < 1) {
        return colormapColor(fraction(value, min, max));
    }
    const int band = bandOf(value, min, max, bands);
    return colormapColor((band + 0.5) / bands);
}

Rgba noValueColor() noexcept
{
    return dimensionColor(1) | (0xFFU << 24U);
}

QImage colormapImage(int width)
{
    const int w = std::max(width, 2);
    QImage image(w, 1, QImage::Format_RGBA8888);
    for (int i = 0; i < w; ++i) {
        const Rgba c = colormapColor(static_cast<double>(i) / (w - 1));
        image.setPixel(i, 0,
                       qRgba(static_cast<int>(c & 0xFFU), static_cast<int>((c >> 8U) & 0xFFU),
                             static_cast<int>((c >> 16U) & 0xFFU), 255));
    }
    return image;
}

} // namespace fem::render
