#include <fem/render/result_scene.hpp>

#include <limits>
#include <utility>

namespace fem::render {

namespace {

// Consecutive samples at the same place of the bar: both sides of a load.
bool samePoint(const core::Station& a, const core::Station& b)
{
    return a.x == b.x && a.node == b.node;
}

} // namespace

std::shared_ptr<const ResultScene> buildResultScene(const core::ShapeSamples& shape,
                                                    std::shared_ptr<const core::Mesh> mesh)
{
    auto scene = std::make_shared<ResultScene>();
    scene->m_bounds = shape.bounds;
    if (!shape.bounds.empty) {
        scene->m_origin = (shape.bounds.min + shape.bounds.max) / 2;
    }
    const std::size_t count = shape.positions.size();
    scene->m_positions.reserve(count);
    scene->m_displacements.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        scene->m_positions.emplace_back((shape.positions[i] - scene->m_origin).cast<float>());
        scene->m_displacements.emplace_back(shape.displacements[i].cast<float>());
    }
    scene->m_stations = shape.stations;
    scene->m_bars = shape.bars;
    const std::size_t nodeCount = mesh ? static_cast<std::size_t>(mesh->nodeCount()) : 0;
    scene->m_nodeSamples.assign(nodeCount, kNoSample);
    const auto reach = [&](core::NodeIndex node, std::uint32_t sample) {
        const auto n = static_cast<std::size_t>(node);
        if (node >= 0 && n < nodeCount && scene->m_nodeSamples[n] == kNoSample) {
            scene->m_nodeSamples[n] = sample;
        }
    };
    for (const core::SampledBar& bar : shape.bars) {
        if (bar.count == 0) {
            continue;
        }
        for (std::uint32_t k = 0; k + 1 < bar.count; ++k) {
            const std::uint32_t at = bar.first + k;
            if (!samePoint(shape.stations[at], shape.stations[at + 1])) {
                scene->m_segmentStarts.push_back(at);
                scene->m_segmentElements.push_back(bar.element);
            }
        }
        // The ends of a bar are its end nodes (the nodes of a rigid link
        // are the bar's own ones).
        if (mesh) {
            const auto nodes = mesh->elementNodes(bar.element);
            reach(nodes[0], bar.first);
            reach(nodes[1], bar.first + bar.count - 1);
        }
    }
    scene->m_mesh = std::move(mesh);
    return scene;
}

std::shared_ptr<const std::vector<float>> segmentValues(const ResultScene& scene,
                                                        const core::FieldSamples& field)
{
    auto values = std::make_shared<std::vector<float>>();
    const std::span<const std::uint32_t> starts = scene.segmentStarts();
    values->reserve(2 * starts.size());
    const auto valueOf = [&](std::size_t sample) {
        return sample < field.values.size() ? static_cast<float>(field.values[sample])
                                            : std::numeric_limits<float>::quiet_NaN();
    };
    for (const std::uint32_t start : starts) {
        values->push_back(valueOf(start));
        values->push_back(valueOf(start + 1));
    }
    return values;
}

} // namespace fem::render
