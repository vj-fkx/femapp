#include <fem/core/beam_state.hpp>

#include <unsupported/Eigen/MatrixFunctions>

namespace fem::core {

StateMatrix matrixExponential(const StateMatrix& a)
{
    // ADR 0006: the only use of the module, so that it can be replaced.
    return a.exp();
}

} // namespace fem::core
