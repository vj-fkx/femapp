#pragma once

#include <Eigen/Core>

namespace fem::core {

// REQ-008b-08, 13: the exact beam of design 008b §5.3 integrates a linear
// system s' = A s + b(s) with A constant along the bar. Its states (14 for a
// 3D beam with warping) plus the augmented states of the loads (3 for a
// global load on an arc, 2 for a linear polynomial) fit in kMaxStates.
inline constexpr int kMaxStates = 19;

// Square, at most kMaxStates: on the stack, never on the heap.
using StateMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxStates, kMaxStates>;

// e^A, to the rounding error (Pade 13 with scaling and squaring, Higham 2005;
// Eigen's MatrixFunctions module, ADR 0006).
[[nodiscard]] StateMatrix matrixExponential(const StateMatrix& a);

} // namespace fem::core
