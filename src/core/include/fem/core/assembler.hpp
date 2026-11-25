#pragma once

#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/run_control.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <cstdint>
#include <span>

namespace fem::core {

using SparseMatrix = Eigen::SparseMatrix<double, Eigen::ColMajor, std::int32_t>;

// REQ-006-14: a concentrated load on one dof.
struct NodalLoad
{
    NodeIndex node = 0;
    DofType type = DofType::U;
    double value = 0.0;
};

struct AssembledSystem
{
    SparseMatrix matrix; // free x free, lower triangle of the symmetric matrix
    Eigen::VectorXd rhs; // free: element vectors and loads, minus K_fp u_p
};

// REQ-006-12 to 14: assembles the elements of `sets` on the free dofs of
// `dofs`, in parallel by element colours (`threads` 0: all cores; 1:
// sequential). Loads on prescribed dofs only count for the reactions.
// REQ-011-09: with `control`, reports the Assembly fraction after each block
// of elements, skips the blocks after a cancellation and then returns
// makeCancelled().
[[nodiscard]] Expected<AssembledSystem> assemble(const Mesh& mesh, const DofMap& dofs,
                                                 std::span<const ElementSet> sets,
                                                 std::span<const NodalLoad> loads, int threads = 0,
                                                 const RunControl* control = nullptr);

// REQ-006-17: K_e u_e - f_e summed over the elements at the prescribed dofs,
// minus the loads there: the reactions, per DofIndex (zero at free dofs).
// REQ-011-09: `control` as in assemble(), with the Recovery stage.
[[nodiscard]] Expected<Eigen::VectorXd> reactions(const Mesh& mesh, const DofMap& dofs,
                                                  std::span<const ElementSet> sets,
                                                  std::span<const NodalLoad> loads,
                                                  const Eigen::VectorXd& values, int threads = 0,
                                                  const RunControl* control = nullptr);

} // namespace fem::core
