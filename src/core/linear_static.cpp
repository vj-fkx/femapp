#include <fem/core/linear_static.hpp>

namespace fem::core {

Expected<LinearSolution> solveLinear(const Mesh& mesh, const DofMap& dofs,
                                     std::span<const ElementSet> sets,
                                     std::span<const NodalLoad> loads, LinearSolver& solver,
                                     int threads)
{
    auto system = assemble(mesh, dofs, sets, loads, threads);
    if (!system) {
        return tl::unexpected(system.error());
    }
    if (auto factorized = solver.factorize(system->matrix); !factorized) {
        return tl::unexpected(factorized.error());
    }
    auto free = solver.solve(system->rhs);
    if (!free) {
        return tl::unexpected(free.error());
    }

    LinearSolution solution;
    solution.values.resize(dofs.size());
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        const std::int32_t eq = dofs.equation(d);
        solution.values[d] = eq < 0 ? dofs.prescribedValue(d) : (*free)[eq];
    }
    auto forces = reactions(mesh, dofs, sets, loads, solution.values, threads);
    if (!forces) {
        return tl::unexpected(forces.error());
    }
    solution.reactions = std::move(*forces);
    return solution;
}

} // namespace fem::core
