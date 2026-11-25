#include "detail/instability.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fem::core::detail {

namespace {

constexpr int kInverseIterations = 3;
constexpr double kMechanismShift = 1e-8; // of the diagonal of each equation
constexpr double kMoving = 1e-3;         // of the largest movement
constexpr double kTieScale = 1e6;        // movements equal to 1e-6 tie, by id

// A fixed start vector in [-0.5, 0.5), by a multiplicative hash: the same on
// every platform (the distributions of <random> are not).
Eigen::VectorXd startVector(Eigen::Index n)
{
    Eigen::VectorXd x(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        const auto h = static_cast<std::uint32_t>(static_cast<std::uint64_t>(i + 1) * 2654435761U);
        x[i] = static_cast<double>(h) / 4294967296.0 - 0.5;
    }
    return x;
}

} // namespace

Expected<Eigen::VectorXd> instabilityMode(const StaticProblem& problem, const SparseMatrix& matrix,
                                          double delta)
{
    const Eigen::Index n = matrix.rows();
    if (n == 0) {
        return makeError("No free dof to move.");
    }
    const Eigen::VectorXd diagonal = matrix.diagonal();
    const double largest = diagonal.cwiseAbs().maxCoeff();
    if (!(largest > 0.0)) {
        return makeError("The matrix has no stiffness.");
    }
    SparseMatrix shifted = matrix;
    if (delta != 0.0) {
        for (Eigen::Index i = 0; i < n; ++i) {
            const double d = std::abs(diagonal[i]) > 0.0 ? std::abs(diagonal[i]) : largest;
            shifted.coeffRef(i, i) += delta * d;
        }
    }
    SparseCholesky solver;
    if (auto factorized = solver.factorizeIndefinite(shifted); !factorized) {
        return tl::unexpected(factorized.error());
    }
    Eigen::VectorXd x = startVector(n);
    for (int k = 0; k < kInverseIterations; ++k) {
        auto y = solver.solve(x);
        if (!y) {
            return tl::unexpected(y.error());
        }
        const double norm = y->norm();
        if (!(norm > 0.0) || !std::isfinite(norm)) {
            return makeError("The inverse iteration lost the mode.");
        }
        x = *y / norm;
    }
    return problem.valuesOf(x, false);
}

Eigen::VectorXd scaledMovement(const StaticProblem& problem, const Eigen::VectorXd& mode)
{
    const BoundingBox box = problem.mesh().boundingBox();
    double size = box.isEmpty() ? 0.0 : (box.max - box.min).norm();
    if (!(size > 0.0)) {
        size = 1.0;
    }
    const DofMap& dofs = problem.dofs();
    Eigen::VectorXd movement(mode.size());
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        double scale = 1.0;
        switch (dofs.typeOf(d)) {
        case DofType::RX:
        case DofType::RY:
        case DofType::RZ:
            scale = size;
            break;
        case DofType::Warp:
            scale = size * size;
            break;
        default:
            break;
        }
        movement[d] = std::abs(mode[d]) * scale;
    }
    return movement;
}

InstabilitySite mechanismSite(const StaticProblem& problem, const Eigen::VectorXd& mode)
{
    InstabilitySite site;
    const Eigen::VectorXd movement = scaledMovement(problem, mode);
    if (movement.size() == 0) {
        return site;
    }
    Eigen::Index most = 0;
    const double largest = movement.maxCoeff(&most);
    if (!(largest > 0.0)) {
        return site;
    }
    site.dof = static_cast<DofIndex>(most);

    const Mesh& mesh = problem.mesh();
    const DofMap& dofs = problem.dofs();
    std::vector<double> ofNode(static_cast<std::size_t>(mesh.nodeCount()), 0.0);
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        double& node = ofNode[static_cast<std::size_t>(dofs.nodeOf(d))];
        node = std::max(node, movement[d]);
    }
    std::vector<std::pair<double, ElementIndex>> moving;
    for (const ElementSet& set : problem.sets()) {
        for (const ElementIndex e : set.elements) {
            double m = 0.0;
            for (const NodeIndex node : mesh.elementNodes(e)) {
                m = std::max(m, ofNode[static_cast<std::size_t>(node)]);
            }
            if (m > kMoving * largest) {
                // Equal movements (to 1e-6 of the largest) tie, broken by id.
                moving.emplace_back(std::round(m / largest * kTieScale), e);
            }
        }
    }
    std::ranges::sort(moving, [&](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return mesh.elementId(a.second) < mesh.elementId(b.second);
    });
    for (const auto& [m, e] : moving) {
        if (site.elements.empty() || site.elements.back() != e) {
            site.elements.push_back(e);
        }
    }
    return site;
}

std::string elementList(const Mesh& mesh, const std::vector<ElementIndex>& elements)
{
    std::string text = elements.size() == 1 ? "element " : "elements ";
    const std::size_t shown = std::min(elements.size(), kListedElements);
    for (std::size_t i = 0; i < shown; ++i) {
        text += (i == 0 ? "" : ", ") + std::to_string(mesh.elementId(elements[i]));
    }
    if (elements.size() > shown) {
        text += " and " + std::to_string(elements.size() - shown) + " more";
    }
    return text;
}

std::vector<ElementIndex> elementsAtNode(const StaticProblem& problem, NodeIndex node)
{
    const Mesh& mesh = problem.mesh();
    std::vector<ElementIndex> found;
    for (const ElementSet& set : problem.sets()) {
        for (const ElementIndex e : set.elements) {
            if (std::ranges::find(mesh.elementNodes(e), node) != mesh.elementNodes(e).end()) {
                found.push_back(e);
            }
        }
    }
    std::ranges::sort(found, [&](ElementIndex a, ElementIndex b) {
        return mesh.elementId(a) < mesh.elementId(b);
    });
    const auto [first, last] = std::ranges::unique(found);
    found.erase(first, last);
    return found;
}

tl::unexpected<Error> mechanismError(const StaticProblem& problem, const SparseMatrix& matrix,
                                     const SparseCholesky& failed, const Error& fallback)
{
    if (auto mode = instabilityMode(problem, matrix, kMechanismShift)) {
        const InstabilitySite site = mechanismSite(problem, *mode);
        if (site.dof >= 0 && !site.elements.empty()) {
            return makeError("The structure is a mechanism: " + problem.dofLabel(site.dof) +
                             " can move without resistance, with the " +
                             elementList(problem.mesh(), site.elements) +
                             " (a support may be missing).");
        }
    }
    // The node and dof of the pivot alone (REQ-007-15).
    const auto eq = failed.singularEquation();
    if (!eq) {
        return tl::unexpected(fallback);
    }
    return makeError("The structure is a mechanism: " + problem.equationLabel(*eq) +
                     " can move without resistance (a support may be missing).");
}

} // namespace fem::core::detail
