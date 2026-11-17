#pragma once

#include <fem/core/expected.hpp>
#include <fem/core/model.hpp>
#include <fem/core/static_analysis.hpp>

#include <Eigen/Core>
#include <optional>
#include <string>
#include <vector>

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

// REQ-008c-09, 13: the options of the linear buckling analysis.
struct BucklingOptions
{
    StaticOptions base;
    int count = 1;            // the factors wanted
    double tolerance = 1e-10; // relative, on the factors
};

// REQ-008c-09, 11: a buckling factor and its mode: the load case times
// `factor` makes the structure unstable. The mode is normalized with its
// largest nodal translation 1 (its largest rotation without translations);
// the beams give its shape along each bar by at(x).displacement.
struct BucklingMode
{
    double factor = 0.0;
    std::vector<Eigen::Vector3d> displacements; // per node
    std::vector<Eigen::Vector3d> rotations;
    std::vector<std::optional<BeamResult>> beams;
};

struct BucklingResult
{
    std::vector<BucklingMode> modes; // increasing factors
    std::string note;                // REQ-008c-12: fewer factors than asked, and why
};

// REQ-008c-09 to 12 (design 008c §5.6, ADR 0007): the smallest positive
// load factors of the model's load case (all of its actions) at which its
// trusses and beams buckle, exact in the bars, with their modes. The axial
// forces come from the linear analysis. Fails with the issues of
// validate(), a mechanism, or when no bar is compressed.
[[nodiscard]] Expected<BucklingResult> runLinearBuckling(const Model& model,
                                                         const BucklingOptions& options = {});

} // namespace fem::core
