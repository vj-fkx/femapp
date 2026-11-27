#include "pick_util.hpp"

#include <fem/render/result_picker.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

namespace fem::render {

namespace {

// The element of the bar that holds a sample.
core::ElementIndex elementOf(const ResultScene& scene, std::uint32_t sample)
{
    const auto bars = scene.bars();
    const auto after = std::ranges::upper_bound(bars, sample, {}, &core::SampledBar::first);
    if (after == bars.begin()) {
        return -1;
    }
    return std::prev(after)->element;
}

} // namespace

std::optional<ResultPick> pickResult(const ResultScene& scene, double scale, const Camera& camera,
                                     const Vec2& pixel, double tolerancePx)
{
    const detail::Projector projector(camera, scene.origin());
    const auto positions = scene.positions();
    const auto displacements = scene.displacements();
    const auto drawn = [&](std::uint32_t sample) {
        return projector.project(Eigen::Vector3d(positions[sample].cast<double>() +
                                                 scale * displacements[sample].cast<double>()));
    };

    // A node within the tolerance wins.
    std::optional<ResultPick> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    const auto nodes = scene.nodeSamples();
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const std::uint32_t sample = nodes[n];
        if (sample == kNoSample) {
            continue;
        }
        const detail::ScreenPoint p = drawn(sample);
        const double distance = p.inFront ? (p.pixel - pixel).norm() : bestDistance;
        if (distance <= tolerancePx && distance < bestDistance) {
            bestDistance = distance;
            best = ResultPick{.element = elementOf(scene, sample),
                              .station = scene.stations()[sample],
                              .node = static_cast<core::NodeIndex>(n)};
        }
    }
    if (best) {
        return best;
    }

    const auto starts = scene.segmentStarts();
    const auto elements = scene.segmentElements();
    for (std::size_t s = 0; s < starts.size(); ++s) {
        const std::uint32_t a = starts[s];
        const detail::ScreenPoint pa = drawn(a);
        const detail::ScreenPoint pb = drawn(a + 1);
        if (!pa.inFront || !pb.inFront) {
            continue;
        }
        const auto [distance, u] = detail::distanceToSegment(pixel, pa.pixel, pb.pixel);
        if (distance <= tolerancePx && distance < bestDistance) {
            bestDistance = distance;
            const core::Station& from = scene.stations()[a];
            const core::Station& to = scene.stations()[a + 1];
            best = ResultPick{.element = elements[s],
                              .station = core::Station{.x = from.x + u * (to.x - from.x),
                                                       .side = core::Side::After,
                                                       .node = -1},
                              .node = -1};
        }
    }
    return best;
}

} // namespace fem::render
