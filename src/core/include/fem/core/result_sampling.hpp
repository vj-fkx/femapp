#pragma once

// Feature 012 (design §5.2 to 5.4): a result sampled along its bars, for the
// display of the deformed shape and of a quantity in colors: the points of
// every bar, where they are and how they move.

#include <fem/core/beam_result.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fem::core {

// REQ-012-09: what the bars can be colored by.
enum class ResultQuantity : std::uint8_t {
    Ux,
    Uy,
    Uz,
    UMagnitude, // global displacements and their size
    Rx,
    Ry,
    Rz, // global rotations
    N,
    Vy,
    Vz,
    T,
    My,
    Mz, // internal forces, principal local axes (REQ-008-21)
};
inline constexpr int kResultQuantityCount = 13;

// The response to draw: a static result or a buckling mode.
struct ResultFields
{
    std::shared_ptr<const Mesh> mesh;
    std::span<const Eigen::Vector3d> displacements; // per node
    std::span<const Eigen::Vector3d> rotations;     // per node
    std::span<const std::optional<BeamResult>> beams;
    // Per element, NaN for elements that are not bars: finite marks the
    // trusses (bars without a beam result).
    std::span<const double> axialForce;
    bool mode = false; // a buckling mode: no internal forces
};
[[nodiscard]] ResultFields fieldsOf(const StaticResult& result);
// The mode's shape; the bars are those of the static result it came from.
[[nodiscard]] ResultFields fieldsOf(const StaticResult& base, const BucklingMode& mode);

// A point of a bar: x from the start of its flexible length (from the start
// node for a truss) and the side of a concentrated load there. `node` is
// set on the ends of a rigid link, the bar's node itself.
struct Station
{
    double x = 0.0;
    Side side = Side::After;
    NodeIndex node = -1;
};

struct SampledBar
{
    ElementIndex element = -1;
    std::uint32_t first = 0; // its first sample
    std::uint32_t count = 0; // consecutive samples make its segments
};

struct ShapeSamples
{
    std::vector<SampledBar> bars;               // in the order of the elements
    std::vector<Station> stations;              // per sample
    std::vector<Eigen::Vector3d> positions;     // per sample, global, original
    std::vector<Eigen::Vector3d> displacements; // per sample, global
    BoundingBox bounds;                         // of the positions
};

// The samples of a beam: from the budget when 0, else this odd count >= 3.
inline constexpr std::size_t kSampleBudget = 1'000'000;
inline constexpr int kMinSamplesPerBeam = 5;
inline constexpr int kMaxSamplesPerBeam = 17;
struct SampleOptions
{
    int perBeam = 0;
    int threads = 0; // 0: all cores
};

// The uniform samples of each of `beams` beams: kSampleBudget / beams,
// odd, within [kMinSamplesPerBeam, kMaxSamplesPerBeam].
[[nodiscard]] int samplesPerBeam(std::size_t beams) noexcept;

// REQ-012-05, 10: the stations of a beam, in order: the start node before
// a rigid link; perBeam uniform points from 0 to its length (the middle
// among them); both sides of each concentrated load inside it; the
// junctions of the parts; the end node after a rigid link. Repeated ones
// are merged.
[[nodiscard]] std::vector<Station> beamStations(const BeamResult& beam, int perBeam,
                                                NodeIndex startLink = -1, NodeIndex endLink = -1);

// REQ-012-05: the stations of every bar and the shape there: beams from
// placementAt and at(); trusses from their nodes. Other elements are left
// out. The same for any number of threads.
[[nodiscard]] ShapeSamples sampleShape(const ResultFields& fields,
                                       const SampleOptions& options = {});

} // namespace fem::core
