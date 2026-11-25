#pragma once

// REQ-011-18 (design 011 §5.2a): where a structure is unstable. The mode is
// the displacement that the matrix lets happen without resistance; it is
// computed only after an analysis has failed.

#include "detail/static_stages.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <string>
#include <vector>

namespace fem::core::detail {

// The longest list of elements a message shows; the rest is "and N more".
inline constexpr std::size_t kListedElements = 10;

struct InstabilitySite
{
    DofIndex dof = -1;                  // the dof that moves the most (scaled)
    std::vector<ElementIndex> elements; // the elements involved, the most first
};

// The mode, per DofIndex (the prescribed dofs zero): a few inverse
// iterations on K + delta D (D the diagonal of K) from a fixed start vector,
// with |mode| = 1. `matrix` is the lower triangle on the free dofs.
[[nodiscard]] Expected<Eigen::VectorXd> instabilityMode(const StaticProblem& problem,
                                                        const SparseMatrix& matrix, double delta);

// The movement of each dof of `mode`, comparable across kinds: rotations
// times the size of the mesh, warping times its square.
[[nodiscard]] Eigen::VectorXd scaledMovement(const StaticProblem& problem,
                                             const Eigen::VectorXd& mode);

// Mechanism: the dof that moves the most and the elements that move (some
// node above 1e-3 of the largest movement), the most first.
[[nodiscard]] InstabilitySite mechanismSite(const StaticProblem& problem,
                                            const Eigen::VectorXd& mode);

// "elements 3, 4, 7 and 2 more" (or "element 3"): the external ids, at most
// kListedElements.
[[nodiscard]] std::string elementList(const Mesh& mesh, const std::vector<ElementIndex>& elements);

// The elements of the problem that end at `node`, by external id.
[[nodiscard]] std::vector<ElementIndex> elementsAtNode(const StaticProblem& problem,
                                                       NodeIndex node);

// REQ-007-15, REQ-011-18: the error of a matrix that failed to factorize as
// positive definite: the mechanism with its node, dof and elements; with
// the node and dof of the pivot alone if the mode cannot be computed.
[[nodiscard]] tl::unexpected<Error> mechanismError(const StaticProblem& problem,
                                                   const SparseMatrix& matrix,
                                                   const SparseCholesky& failed,
                                                   const Error& fallback);

} // namespace fem::core::detail
