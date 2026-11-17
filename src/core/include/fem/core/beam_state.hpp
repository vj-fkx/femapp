#pragma once

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>

#include <Eigen/Core>
#include <array>
#include <span>

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
struct StateBarMatrices
{
    StateBarMatrix stiffness;
    StateBarVector loads;
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
[[nodiscard]] BarState stateAt(const BeamData& beam, double length, const SpanLoads& loads,
                               bool planar, const StateBarVector& ends, double s,
                               Side side = Side::After);

// REQ-008b-07, 09, 11, 13: the exact beam element (design 008b §5.3, 5.4)
// for a Line3 arc (no dofs at its middle node) and, in the core only, a
// Line2 with warping: the exact flexible length, then the connections
// (springs, releases, the free warping of an end condensed, the restrained
// one removed), the offsets and the rotation of each end.
class ExactBeamElement final : public ElementFormulation
{
public:
    // `beams` and `loads` are indexed by ElementIndex and must outlive the
    // formulation.
    ExactBeamElement(ElementKind kind, std::span<const BeamData> beams,
                     std::span<const SpanLoads> loads);

    [[nodiscard]] DofMask dofsAt(ElementType type, int localNode) const override;
    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override;

private:
    ElementKind m_kind;
    std::span<const BeamData> m_beams;
    std::span<const SpanLoads> m_loads;
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
