#include <fem/render/result_display.hpp>

#include <algorithm>
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

std::vector<double> legendTicks(double min, double max, int bands)
{
    constexpr int kMaxBandTicks = 10;
    constexpr int kEvenTicks = 5;
    const int steps = bands >= 1 && bands <= kMaxBandTicks ? bands : kEvenTicks;
    std::vector<double> ticks;
    ticks.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        ticks.push_back(i == steps ? max : min + (max - min) * i / steps);
    }
    return ticks;
}

std::vector<PlacedLabel>
placeLabels(std::span<const ResultLabel> labels, double scale,
            const std::function<std::optional<QPointF>(const Eigen::Vector3d&)>& project,
            const std::function<QSizeF(const QString&)>& sizeOf, const QSizeF& view)
{
    std::vector<std::size_t> order(labels.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return labels[i].kind; });
    const QRectF screen(QPointF(0.0, 0.0), view);
    std::vector<PlacedLabel> placed;
    for (const std::size_t i : order) {
        const ResultLabel& label = labels[i];
        const std::optional<QPointF> anchor = project(label.at + scale * label.displacement);
        if (!anchor || !screen.contains(*anchor)) {
            continue;
        }
        const QSizeF size = sizeOf(label.text);
        const QRectF box(
            QPointF(anchor->x() + kLabelOffsetPx, anchor->y() - kLabelOffsetPx - size.height()),
            size);
        const bool covers = std::ranges::any_of(
            placed, [&](const PlacedLabel& other) { return other.box.intersects(box); });
        if (!covers) {
            placed.push_back({.index = i, .anchor = *anchor, .box = box});
        }
    }
    return placed;
}

} // namespace fem::render
