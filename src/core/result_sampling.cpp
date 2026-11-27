#include <fem/core/element_type.hpp>
#include <fem/core/result_sampling.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace fem::core {

namespace {

// The ends of a rigid link: where the flexible length starts or ends away
// from the node.
bool linked(const Eigen::Vector3d& node, const Eigen::Vector3d& flexibleEnd, double scale)
{
    return (flexibleEnd - node).norm() > 1e-12 * scale;
}

bool isTruss(const ResultFields& fields, std::size_t e)
{
    const bool beam = e < fields.beams.size() && fields.beams[e].has_value();
    if (beam || !fields.mesh || e >= static_cast<std::size_t>(fields.mesh->elementCount())) {
        return false;
    }
    if (fields.mode) {
        // A mode keeps no axial forces: its line elements without a beam.
        const auto type = fields.mesh->elementType(static_cast<ElementIndex>(e));
        return elementTypeInfo(type).dimension == 1;
    }
    return e < fields.axialForce.size() && std::isfinite(fields.axialForce[e]);
}

bool isBeam(const ResultFields& fields, std::size_t e)
{
    return e < fields.beams.size() && fields.beams[e].has_value();
}

// The stations of element e and where they are, before the shape.
std::vector<Station> stationsOf(const ResultFields& fields, std::size_t e, int perBeam)
{
    const Mesh& mesh = *fields.mesh;
    const auto element = static_cast<ElementIndex>(e);
    const auto nodes = mesh.elementNodes(element);
    const Eigen::Vector3d a = mesh.nodeCoordinates(nodes[0]);
    const Eigen::Vector3d b = mesh.nodeCoordinates(nodes[1]);
    if (isTruss(fields, e)) {
        return {Station{.x = 0.0, .side = Side::After, .node = nodes[0]},
                Station{.x = (b - a).norm(), .side = Side::After, .node = nodes[1]}};
    }
    const BeamResult& beam = *fields.beams[e];
    const double scale = std::max({1.0, a.norm(), b.norm()});
    const NodeIndex startLink = linked(a, beam.placementAt(0.0).position, scale) ? nodes[0] : -1;
    const NodeIndex endLink =
        linked(b, beam.placementAt(beam.length()).position, scale) ? nodes[1] : -1;
    return beamStations(beam, perBeam, startLink, endLink);
}

constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

struct Motion
{
    Eigen::Vector3d displacement = Eigen::Vector3d::Zero();
    Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
};

Eigen::Vector3d nodal(std::span<const Eigen::Vector3d> values, NodeIndex node)
{
    const auto n = static_cast<std::size_t>(node);
    return node >= 0 && n < values.size() ? values[n] : Eigen::Vector3d::Zero();
}

// The global displacement and rotation at a point of element e.
Motion motionAt(const ResultFields& fields, std::size_t e, const Station& at)
{
    if (at.node >= 0) {
        return {.displacement = nodal(fields.displacements, at.node),
                .rotation = nodal(fields.rotations, at.node)};
    }
    if (isBeam(fields, e)) {
        const BeamResult& beam = *fields.beams[e];
        const Eigen::Matrix3d axes = beam.placementAt(at.x).axes;
        const BeamPoint point = beam.at(at.x, at.side);
        return {.displacement = axes * point.displacement, .rotation = axes * point.rotation};
    }
    // A truss: linear between its nodes.
    const Mesh& mesh = *fields.mesh;
    const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
    const double length = (mesh.nodeCoordinates(nodes[1]) - mesh.nodeCoordinates(nodes[0])).norm();
    const double t = length > 0.0 ? std::clamp(at.x / length, 0.0, 1.0) : 0.0;
    return {.displacement = (1.0 - t) * nodal(fields.displacements, nodes[0]) +
                            t * nodal(fields.displacements, nodes[1]),
            .rotation = (1.0 - t) * nodal(fields.rotations, nodes[0]) +
                        t * nodal(fields.rotations, nodes[1])};
}

double forceOf(const BeamForces& f, ResultQuantity quantity)
{
    switch (quantity) {
    case ResultQuantity::N:
        return f.n;
    case ResultQuantity::Vy:
        return f.vy;
    case ResultQuantity::Vz:
        return f.vz;
    case ResultQuantity::T:
        return f.t;
    case ResultQuantity::My:
        return f.my;
    case ResultQuantity::Mz:
        return f.mz;
    default:
        return kNoValue;
    }
}

double motionOf(const Motion& m, ResultQuantity quantity)
{
    switch (quantity) {
    case ResultQuantity::Ux:
        return m.displacement.x();
    case ResultQuantity::Uy:
        return m.displacement.y();
    case ResultQuantity::Uz:
        return m.displacement.z();
    case ResultQuantity::UMagnitude:
        return m.displacement.norm();
    case ResultQuantity::Rx:
        return m.rotation.x();
    case ResultQuantity::Ry:
        return m.rotation.y();
    case ResultQuantity::Rz:
        return m.rotation.z();
    default:
        return kNoValue;
    }
}

} // namespace

bool isForce(ResultQuantity quantity) noexcept
{
    return quantity >= ResultQuantity::N;
}

ResultFields fieldsOf(const StaticResult& result)
{
    return ResultFields{.mesh = result.mesh,
                        .displacements = result.displacements,
                        .rotations = result.rotations,
                        .beams = result.beams,
                        .axialForce = result.axialForce,
                        .mode = false};
}

ResultFields fieldsOf(std::shared_ptr<const Mesh> mesh, const BucklingMode& mode)
{
    return ResultFields{.mesh = std::move(mesh),
                        .displacements = mode.displacements,
                        .rotations = mode.rotations,
                        .beams = mode.beams,
                        .axialForce = {},
                        .mode = true};
}

int samplesPerBeam(std::size_t beams) noexcept
{
    if (beams == 0) {
        return kMaxSamplesPerBeam;
    }
    const std::size_t share = kSampleBudget / beams;
    int count = static_cast<int>(std::clamp<std::size_t>(
        share, kMinSamplesPerBeam, static_cast<std::size_t>(kMaxSamplesPerBeam)));
    if (count % 2 == 0) {
        --count;
    }
    return count;
}

std::vector<Station> beamStations(const BeamResult& beam, int perBeam, NodeIndex startLink,
                                  NodeIndex endLink)
{
    const double length = beam.length();
    const int count = std::max(perBeam, 2);
    std::vector<Station> inside;
    inside.reserve(static_cast<std::size_t>(count) + 2 * beam.concentrated().size() +
                   beam.junctions().size());
    for (int k = 0; k < count; ++k) {
        // The last one exactly at the length, the middle exactly at half.
        const double x = k == count - 1 ? length : length * k / (count - 1);
        inside.push_back({.x = x, .side = Side::After, .node = -1});
    }
    for (const SpanPoint& point : beam.concentrated()) {
        if (point.at > 0.0 && point.at < length) {
            inside.push_back({.x = point.at, .side = Side::Before, .node = -1});
            inside.push_back({.x = point.at, .side = Side::After, .node = -1});
        }
    }
    for (const double junction : beam.junctions()) {
        if (junction > 0.0 && junction < length) {
            inside.push_back({.x = junction, .side = Side::After, .node = -1});
        }
    }
    const auto key = [](const Station& s) { return std::tuple(s.x, s.side == Side::After); };
    std::ranges::sort(inside, [&](const Station& p, const Station& q) { return key(p) < key(q); });
    const auto [first, last] = std::ranges::unique(
        inside, [&](const Station& p, const Station& q) { return key(p) == key(q); });
    inside.erase(first, last);

    std::vector<Station> out;
    out.reserve(inside.size() + 2);
    if (startLink >= 0) {
        out.push_back({.x = 0.0, .side = Side::After, .node = startLink});
    }
    out.insert(out.end(), inside.begin(), inside.end());
    if (endLink >= 0) {
        out.push_back({.x = length, .side = Side::After, .node = endLink});
    }
    return out;
}

ShapeSamples sampleShape(const ResultFields& fields, const SampleOptions& options)
{
    ShapeSamples out;
    if (!fields.mesh) {
        return out;
    }
    const Mesh& mesh = *fields.mesh;
    const auto elementCount = static_cast<std::size_t>(mesh.elementCount());
    std::vector<std::size_t> bars;
    std::size_t beamCount = 0;
    for (std::size_t e = 0; e < elementCount; ++e) {
        if (isBeam(fields, e)) {
            bars.push_back(e);
            ++beamCount;
        } else if (isTruss(fields, e)) {
            bars.push_back(e);
        }
    }
    const int perBeam = options.perBeam > 0 ? options.perBeam : samplesPerBeam(beamCount);

    tbb::task_arena arena(options.threads > 0 ? options.threads : tbb::task_arena::automatic);
    // First pass: the stations of each bar; then their places in the lists.
    std::vector<std::vector<Station>> stations(bars.size());
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, bars.size()),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              for (std::size_t i = range.begin(); i != range.end(); ++i) {
                                  stations[i] = stationsOf(fields, bars[i], perBeam);
                              }
                          });
    });
    out.bars.resize(bars.size());
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const auto count = static_cast<std::uint32_t>(stations[i].size());
        out.bars[i] = {
            .element = static_cast<ElementIndex>(bars[i]), .first = total, .count = count};
        total += count;
    }
    out.stations.resize(total);
    out.positions.resize(total);
    out.displacements.resize(total);

    // Second pass: where each sample is and how it moves.
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, bars.size()),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              for (std::size_t i = range.begin(); i != range.end(); ++i) {
                                  const std::size_t e = bars[i];
                                  const SampledBar& bar = out.bars[i];
                                  for (std::uint32_t k = 0; k < bar.count; ++k) {
                                      const std::size_t at = bar.first + k;
                                      const Station& station = stations[i][k];
                                      out.stations[at] = station;
                                      if (station.node >= 0) {
                                          // A truss end or the node of a rigid link.
                                          const auto n = static_cast<std::size_t>(station.node);
                                          out.positions[at] = mesh.nodeCoordinates(station.node);
                                          out.displacements[at] = n < fields.displacements.size()
                                                                      ? fields.displacements[n]
                                                                      : Eigen::Vector3d::Zero();
                                          continue;
                                      }
                                      const BeamResult& beam = *fields.beams[e];
                                      const BeamPlacement placement = beam.placementAt(station.x);
                                      out.positions[at] = placement.position;
                                      out.displacements[at] =
                                          placement.axes *
                                          beam.at(station.x, station.side).displacement;
                                  }
                              }
                          });
    });

    for (const Eigen::Vector3d& p : out.positions) {
        if (out.bounds.empty) {
            out.bounds = {.min = p, .max = p, .empty = false};
        } else {
            out.bounds.min = out.bounds.min.cwiseMin(p);
            out.bounds.max = out.bounds.max.cwiseMax(p);
        }
    }
    return out;
}

FieldSamples sampleField(const ResultFields& fields, const ShapeSamples& shape,
                         ResultQuantity quantity, int threads)
{
    FieldSamples out;
    out.quantity = quantity;
    out.values.assign(shape.stations.size(), kNoValue);
    tbb::task_arena arena(threads > 0 ? threads : tbb::task_arena::automatic);
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, shape.bars.size()),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              for (std::size_t i = range.begin(); i != range.end(); ++i) {
                                  const SampledBar& bar = shape.bars[i];
                                  for (std::uint32_t k = 0; k < bar.count; ++k) {
                                      const std::size_t at = bar.first + k;
                                      out.values[at] = valueAt(fields, bar.element,
                                                               shape.stations[at], quantity);
                                  }
                              }
                          });
    });
    bool found = false;
    for (std::size_t i = 0; i < out.values.size(); ++i) {
        const double value = out.values[i];
        if (!std::isfinite(value)) {
            continue;
        }
        const auto sample = static_cast<std::uint32_t>(i);
        if (!found || value < out.min.value) {
            out.min = {.value = value, .sample = sample};
        }
        if (!found || value > out.max.value) {
            out.max = {.value = value, .sample = sample};
        }
        found = true;
    }
    return out;
}

double valueAt(const ResultFields& fields, ElementIndex element, const Station& at,
               ResultQuantity quantity)
{
    const auto e = static_cast<std::size_t>(element);
    const bool beam = isBeam(fields, e);
    if (!beam && !isTruss(fields, e)) {
        return kNoValue;
    }
    if (!isForce(quantity)) {
        return motionOf(motionAt(fields, e, at), quantity);
    }
    if (fields.mode) {
        return kNoValue;
    }
    const std::optional<BeamResult>* result = beam ? &fields.beams[e] : nullptr;
    if (result == nullptr || !result->has_value()) {
        return quantity == ResultQuantity::N ? fields.axialForce[e] : 0.0;
    }
    // A rigid link takes the forces of the flexible end next to it, whose x
    // its station has.
    return forceOf((*result)->at(at.x, at.side).forces, quantity);
}

Eigen::Vector3d positionAt(const ResultFields& fields, ElementIndex element, const Station& at)
{
    const auto e = static_cast<std::size_t>(element);
    if (!fields.mesh) {
        return Eigen::Vector3d::Zero();
    }
    const Mesh& mesh = *fields.mesh;
    if (at.node >= 0) {
        return mesh.nodeCoordinates(at.node);
    }
    if (e < fields.beams.size()) {
        if (const std::optional<BeamResult>& beam = fields.beams[e]; beam.has_value()) {
            return beam->placementAt(at.x).position;
        }
    }
    if (!isTruss(fields, e)) {
        return Eigen::Vector3d::Zero();
    }
    const auto nodes = mesh.elementNodes(element);
    const Eigen::Vector3d a = mesh.nodeCoordinates(nodes[0]);
    const Eigen::Vector3d b = mesh.nodeCoordinates(nodes[1]);
    const double length = (b - a).norm();
    const double t = length > 0.0 ? std::clamp(at.x / length, 0.0, 1.0) : 0.0;
    return (1.0 - t) * a + t * b;
}

Eigen::Vector3d displacementAt(const ResultFields& fields, ElementIndex element, const Station& at)
{
    const auto e = static_cast<std::size_t>(element);
    if (!isBeam(fields, e) && !isTruss(fields, e)) {
        return Eigen::Vector3d::Zero();
    }
    return motionAt(fields, e, at).displacement;
}

double autoScale(const ShapeSamples& shape) noexcept
{
    double largest = 0.0;
    for (const Eigen::Vector3d& d : shape.displacements) {
        const double size = d.norm();
        if (std::isfinite(size)) {
            largest = std::max(largest, size);
        }
    }
    const double side = shape.bounds.empty ? 0.0 : (shape.bounds.max - shape.bounds.min).maxCoeff();
    if (!(largest > 0.0) || !(side > 0.0)) {
        return 1.0;
    }
    return kAutoScaleFraction * side / largest;
}

} // namespace fem::core
