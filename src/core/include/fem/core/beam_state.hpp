#pragma once

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>

#include <Eigen/Core>

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

// The local dofs of both ends of an exact bar, at most 7 per end:
// (u, v, w, rx, ry, rz, warping) with warping, (u, v, w, rx, ry, rz) without,
// (u, v, rz) in the plane.
inline constexpr int kMaxStateDofs = 14;
using StateBarMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxStateDofs, kMaxStateDofs>;
using StateBarVector = Eigen::Matrix<double, Eigen::Dynamic, 1, 0, kMaxStateDofs, 1>;

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

} // namespace fem::core
