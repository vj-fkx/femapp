#pragma once

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>

#include <Eigen/Core>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace fem::core {

// REQ-008b-08, 13: the exact beam of design 008b §5.3 integrates a linear
// system s' = A s + b(s) with A constant along the bar. Its states (14 for a
// 3D beam with warping) plus the augmented states of the loads (6 for a
// linear global load on an arc, 2 for a linear polynomial) fit in
// kMaxStates.
inline constexpr int kMaxStates = 20;

// Square, at most kMaxStates: on the stack, never on the heap.
using StateMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxStates, kMaxStates>;

// e^A, to the rounding error (Pade 13 with scaling and squaring, Higham 2005;
// Eigen's MatrixFunctions module, ADR 0006).
[[nodiscard]] StateMatrix matrixExponential(const StateMatrix& a);

// Dofs per end: 3 in the plane, 7 with warping, 6 otherwise.
[[nodiscard]] int stateDofs(const BeamData& beam, bool planar);

// The flexible length on its own, exact (design 008b §5.3): the stiffness
// on the local principal dofs of both ends at the centroid, and the
// equivalent loads of the span loads with both ends held (the counterpart
// of flexibleStiffness and equivalentLoads of the 008, which it reproduces
// for a straight bar without warping nor foundation). A curved bar has the
// constant turn rate BeamData::curvature; the dofs of each end are in the
// local axes at that end.
// Feature 008c: with the axial force of `beam.axialForce`, the second-order
// bar (design 008c §5.1), and the negative pivots met when condensing its
// parts, for the Sturm count (§5.4); kUnstableBar when the compression is
// beyond the shear buckling of any part (|N| near G As).
inline constexpr int kUnstableBar = 1 << 20;
struct StateBarMatrices
{
    StateBarMatrix stiffness;
    StateBarVector loads;
    int negativePivots = 0;
};
[[nodiscard]] StateBarMatrices stateBarMatrices(const BeamData& beam, double length,
                                                const SpanLoads& loads, bool planar);

// REQ-008b-19: the state of the exact bar at s in [0, length], from the
// displacements of its ends (the local dofs of stateBarMatrices): exact,
// including the jumps at concentrated loads (`side`). In the principal
// local axes at s, on the centroid; the moment Mx is about the centroid.
struct BarState
{
    Eigen::Vector3d displacement = Eigen::Vector3d::Zero();
    Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
    double warping = 0.0;                            // psi, the rate of twist
    Eigen::Vector3d force = Eigen::Vector3d::Zero(); // N, Vy, Vz
    Eigen::Vector3d moment = Eigen::Vector3d::Zero();
    double bimoment = 0.0; // B = -E Iw psi'
};
// Feature 008c: with `junctions`, the local dofs of the junctions between
// the parts of the engine (partStiffnesses) are given instead of solved
// for: the shape of a bar buckling with its ends held (design 008c §5.6).
[[nodiscard]] BarState stateAt(const BeamData& beam, double length, const SpanLoads& loads,
                               bool planar, const StateBarVector& ends, double s,
                               Side side = Side::After, std::span<const double> junctions = {});

// Feature 008c (design 008c §5.4): the negative eigenvalues of a small
// symmetric matrix (its inertia), by cyclic Jacobi rotations, without
// allocating. Used for the Sturm count of the parts and the connections.
[[nodiscard]] int negativeEigenvalues(const StateBarMatrix& matrix);

// Feature 008c: the stiffness of each part of the engine, without loads,
// in order along the bar (the dofs of both ends of each part).
[[nodiscard]] std::vector<StateBarMatrix> partStiffnesses(const BeamData& beam, double length,
                                                          bool planar);

// Feature 008c (design 008c §5.6): the buckling mode of an exact bar with
// its nodes held, at the axial force of `beam` (a buckling factor where the
// internal dofs are singular): its flexible end dofs (zero where held by
// the nodes) and its junctions; nothing when it has no internal dofs.
struct ExactInternalMode
{
    StateBarVector ends;
    std::vector<double> junctions;
};
[[nodiscard]] std::optional<ExactInternalMode> exactInternalMode(const BeamData& beam,
                                                                 ElementKind kind,
                                                                 const Eigen::Vector3d& x1,
                                                                 const Eigen::Vector3d& x2);

// REQ-008b-07, 09, 11, 13: the exact beam element (design 008b §5.3, 5.4)
// for a Line3 arc (no dofs at its middle node) and, in the core only, a
// Line2 with warping: the exact flexible length, then the connections
// (springs, releases, the free warping of an end condensed, the restrained
// one removed), the offsets and the rotation of each end.
class ExactBeamElement final : public ElementFormulation
{
public:
    // `beams` and `loads` are indexed by ElementIndex and must outlive the
    // formulation. Feature 008c: with `negativePivots` (indexed the same),
    // compute() writes the negative pivots of the element's internal
    // condensations, for the Sturm count (design 008c §5.4); each element
    // writes only its own entry.
    ExactBeamElement(ElementKind kind, std::span<const BeamData> beams,
                     std::span<const SpanLoads> loads, std::span<int> negativePivots = {});

    [[nodiscard]] DofMask dofsAt(ElementType type, int localNode) const override;
    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override;

private:
    ElementKind m_kind;
    std::span<const BeamData> m_beams;
    std::span<const SpanLoads> m_loads;
    std::span<int> m_negativePivots;
};

// The flexible length of an exact beam: along the arc, or between the ends
// of the offsets.
[[nodiscard]] double exactLength(const BeamData& beam, const Eigen::Vector3d& x1,
                                 const Eigen::Vector3d& x2);

// REQ-008b-19: the local dofs of both ends of the exact flexible length
// (those of stateBarMatrices) from the values of the element's nodal dofs,
// in the element's order (the internal dofs of the connections recovered);
// with `endLoads`, also the forces the nodes apply on the flexible length.
[[nodiscard]] StateBarVector exactBeamEnds(const BeamData& beam, ElementKind kind,
                                           const SpanLoads& loads, const Eigen::Vector3d& x1,
                                           const Eigen::Vector3d& x2,
                                           std::span<const double> nodeValues,
                                           StateBarVector* endLoads = nullptr);

// The forces that the nodes apply on the exact flexible length, K d - f,
// from the local dofs of its ends.
[[nodiscard]] StateBarVector exactEndLoads(const BeamData& beam, double length,
                                           const SpanLoads& loads, bool planar,
                                           const StateBarVector& ends);

// REQ-008b-13: false when the connections let the exact flexible length
// move without strain.
[[nodiscard]] bool hasStableExactConnections(const BeamData& beam, ElementKind kind, double length);

} // namespace fem::core
