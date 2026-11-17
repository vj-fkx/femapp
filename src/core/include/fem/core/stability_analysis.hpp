#pragma once

#include <fem/core/expected.hpp>
#include <fem/core/model.hpp>
#include <fem/core/static_analysis.hpp>

namespace fem::core {

// REQ-008c-05, 13: the options of the second-order analysis.
struct SecondOrderOptions
{
    StaticOptions base;
    double tolerance = 1e-8; // relative change of displacements and axial forces
    int maxIterations = 50;
};

// REQ-008c-01, 06: the result of the linear analysis with the effect of the
// axial forces: the beams' at(x) includes the moment N delta.
struct SecondOrderResult
{
    StaticResult result;
    int iterations = 0;
    double change = 0.0; // the last relative change
};

// REQ-008c-01, 05, 07, 08 (design 008c §5.5): the second-order static
// analysis of trusses and beams: small displacements, loads that keep their
// direction, the axial force of each bar in its stiffness, iterated until
// it converges. Fails with the issues of validate(), a mechanism, a load at
// or above the critical one (with the node and direction, or the element),
// or no convergence within the iterations.
[[nodiscard]] Expected<SecondOrderResult>
runSecondOrderStatic(const Model& model, const SecondOrderOptions& options = {});

} // namespace fem::core
