// REQ-008c-09 to 12: the linear buckling analysis (design 008c §5.6, ADR
// 0007). The axial forces of the linear analysis, times lambda, enter the
// bars exactly, so the buckling factors solve the nonlinear eigenproblem
// K(lambda) phi = 0:
//  - estimates by subspace iteration on K0 phi = mu G phi, G = -dK/dlambda
//    at 0 (central difference), with the factorization of the linear
//    analysis;
//  - one factorization at sigma below the first estimate, whose count must
//    be zero (no factor below it);
//  - each factor refined by residual inverse iteration (Neumaier, 1985)
//    with that factorization, lambda by the secant on x^T K(lambda) x = 0,
//    deflating the converged modes by the nonlinear orthogonality
//    phi_j^T (K(l) - K(l_j)) / (l - l_j) x = 0;
//  - a final Sturm count (Wittrick-Williams) above the last factor: it
//    must equal the factors found, or the missing ones are bisected.

#include "detail/static_stages.hpp"

#include <fem/core/linear_solver.hpp>
#include <fem/core/stability_analysis.hpp>

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fem::core {

namespace {

using detail::StaticProblem;

constexpr double kDerivativeStep = 1e-4; // of the load factor, for G
constexpr int kSubspaceIterations = 40;
constexpr int kSubspaceExtra = 4;
// The estimates only start the refinement: they need not be close.
constexpr double kEstimateSettled = 1e-4;
constexpr double kFirstShift = 1e-2;                      // below the first estimate, for the count
constexpr double kModeShift = 1e-3;                       // below the estimate of each other factor
constexpr std::array<double, 2> kBrackets = {5e-3, 1e-5}; // of the interpolation
constexpr double kWideTolerance = 1e-7;
constexpr int kRecentring = 12;
constexpr int kRefinementSteps = 80;
constexpr double kResidual = 1e-9;     // of K(lambda) x against K0 x
constexpr double kCountMargin = 1e-6;  // above the last factor
constexpr double kSameFactor = 1e-7;   // repeated factors
constexpr double kInsideMargin = 1e-7; // about a factor inside a bar
constexpr int kBisections = 200;
constexpr double kBisectionPrecision = 1e-13;
constexpr int kDoublings = 60;
constexpr double kRefinementReach = 2.0; // of the starting lambda: a local search

Eigen::VectorXd times(const SparseMatrix& lower, const Eigen::VectorXd& x)
{
    return lower.selfadjointView<Eigen::Lower>() * x;
}

// K(lambda): the problem assembled with lambda times the reference axial
// forces; the count of the negative pivots inside its bars.
class Pencil
{
public:
    Pencil(StaticProblem& problem, std::vector<ReferenceProfile> reference)
        : m_problem(problem)
        , m_reference(std::move(reference))
    {}

    [[nodiscard]] Expected<SparseMatrix> at(double lambda, std::int64_t* internal = nullptr)
    {
        std::vector<ReferenceProfile> scaled(m_reference.size());
        for (std::size_t e = 0; e < scaled.size(); ++e) {
            scaled[e] = m_reference[e].scaled(lambda);
        }
        m_problem.setReference(scaled);
        auto system = m_problem.assemble();
        if (!system) {
            return tl::unexpected(system.error());
        }
        if (internal != nullptr) {
            *internal = m_problem.negativePivots();
        }
        return std::move(system->matrix);
    }

private:
    StaticProblem& m_problem;
    std::vector<ReferenceProfile> m_reference;
};

// J(lambda): the factors below lambda (design 008c §5.4); the factorization
// is kept in `keep` when given. A zero pivot moves lambda a little.
Expected<std::int64_t> sturmCount(Pencil& pencil, double lambda,
                                  std::unique_ptr<SparseCholesky>* keep = nullptr)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::int64_t internal = 0;
        auto matrix = pencil.at(lambda, &internal);
        if (!matrix) {
            return tl::unexpected(matrix.error());
        }
        auto factorization = std::make_unique<SparseCholesky>();
        if (factorization->factorizeIndefinite(*matrix)) {
            const std::int64_t count = factorization->negativePivots() + internal;
            if (keep != nullptr) {
                *keep = std::move(factorization);
            }
            return count;
        }
        lambda *= 1.0 - 1e-8;
    }
    return makeError("The buckling search met a singular matrix it could not step around.");
}

struct Factor
{
    double lambda = 0.0;
    Eigen::VectorXd x;
    SparseMatrix k;           // K(lambda), for the deflation
    ElementIndex inside = -1; // a bar buckling with its nodes held
};

// Residual inverse iteration from (x, lambda) with the factorization at a
// shift near the factor (T23b): K(lambda) is interpolated between two
// assemblies about lambda, first wide then narrow, so that a step costs
// products and not an assembly; lambda by Newton on x^T K(l) x = 0 with
// the slope of the interpolation. False when it does not converge.
Expected<bool> refine(Pencil& pencil, const SparseCholesky& shifted, const SparseMatrix& k0,
                      const std::vector<Factor>& found, double tolerance, Eigen::VectorXd& x,
                      double& lambda, SparseMatrix& kOut)
{
    x.normalize();
    // A Newton step from a poor vector can throw lambda far away, where the
    // bars need many parts: the search is local and gives up there.
    const double origin = lambda;
    // Each width again, centred on the last lambda, until lambda stays
    // inside its bracket: outside, the interpolation extrapolates.
    std::size_t stage = 0;
    for (int round = 0; stage < kBrackets.size() && round < kRecentring; ++round) {
        const bool last = stage + 1 == kBrackets.size();
        const double centre = lambda;
        const double a = lambda * (1.0 - kBrackets.at(stage));
        const double b = lambda * (1.0 + kBrackets.at(stage));
        auto ka = pencil.at(a);
        if (!ka) {
            return tl::unexpected(ka.error());
        }
        auto kb = pencil.at(b);
        if (!kb) {
            return tl::unexpected(kb.error());
        }
        const SparseMatrix slope = (*kb - *ka) / (b - a); // dK / dlambda
        const auto product = [&](double l, const Eigen::VectorXd& v) {
            return Eigen::VectorXd(times(*ka, v) + (l - a) * times(slope, v));
        };
        bool converged = false;
        for (int step = 0; step < kRefinementSteps && !converged; ++step) {
            const double derivative = x.dot(times(slope, x));
            const double next =
                derivative != 0.0 ? lambda - x.dot(product(lambda, x)) / derivative : lambda;
            const double change = std::abs(next - lambda);
            lambda = next;
            // Written so that a NaN lambda also gives up.
            const bool local =
                lambda > origin / kRefinementReach && lambda < origin * kRefinementReach;
            if (!local) {
                return false;
            }
            // A vector that vanished (no mode in the nodal dofs, as a bar
            // buckling with its nodes held) never converges: the count finds
            // that factor.
            const Eigen::VectorXd r = product(lambda, x);
            const double size = times(k0, x).norm();
            if (!(x.norm() > 0.5) || !(size > 0.0)) {
                return false;
            }
            converged = last ? r.norm() <= kResidual * size && change <= tolerance * lambda
                             : change <= kWideTolerance * lambda;
            if (converged) {
                break;
            }
            auto correction = shifted.solve(r);
            if (!correction) {
                return tl::unexpected(correction.error());
            }
            x -= *correction;
            // Deflate the converged modes (nonlinear orthogonality).
            for (const Factor& other : found) {
                Eigen::VectorXd dx;
                Eigen::VectorXd dphi;
                if (std::abs(lambda - other.lambda) > kSameFactor * lambda) {
                    const double gap = lambda - other.lambda;
                    dx = (product(lambda, x) - times(other.k, x)) / gap;
                    dphi = (product(lambda, other.x) - times(other.k, other.x)) / gap;
                } else {
                    dx = times(slope, x);
                    dphi = times(slope, other.x);
                }
                const double norm = other.x.dot(dphi);
                if (norm != 0.0) {
                    x -= other.x * (other.x.dot(dx) / norm);
                }
            }
            x.normalize();
        }
        if (!converged) {
            return false;
        }
        if (std::abs(lambda - centre) > 0.5 * kBrackets.at(stage) * centre) {
            continue; // the same width about the new lambda
        }
        if (last) {
            kOut = *ka + (lambda - a) * slope;
            return true;
        }
        ++stage;
    }
    return false;
}

} // namespace

Expected<BucklingResult> runLinearBuckling(const Model& model, const BucklingOptions& options)
{
    auto prepared = StaticProblem::prepare(model, options.base, true);
    if (!prepared) {
        return tl::unexpected(prepared.error());
    }
    StaticProblem& problem = **prepared;

    // The linear analysis: the reference axial forces and K0.
    auto system = problem.assemble();
    if (!system) {
        return tl::unexpected(system.error());
    }
    if (auto stable = problem.checkDiagonal(system->matrix); !stable) {
        return tl::unexpected(stable.error());
    }
    const SparseMatrix k0 = system->matrix;
    SparseCholesky base;
    if (auto factorized = base.factorize(k0); !factorized) {
        const auto eq = base.singularEquation();
        if (!eq) {
            return tl::unexpected(factorized.error());
        }
        return makeError("The structure is a mechanism: " + problem.equationLabel(*eq) +
                         " can move without resistance (a support may be missing).");
    }
    auto linear = base.solve(system->rhs);
    if (!linear) {
        return tl::unexpected(linear.error());
    }
    std::vector<ReferenceProfile> reference = problem.profilesOf(problem.valuesOf(*linear));
    // REQ-008c-12: no compression, no buckling.
    if (std::ranges::none_of(
            reference, [](const ReferenceProfile& p) { return p.largestCompression() > 0.0; })) {
        return makeError("No bar is compressed by these loads: there is no buckling factor.");
    }
    Pencil pencil(problem, reference);
    const Eigen::Index n = k0.rows();
    const int wanted = std::max(1, options.count);
    BucklingResult out;

    // G = -dK/dlambda at 0, by central difference.
    auto plus = pencil.at(kDerivativeStep);
    auto minus = pencil.at(-kDerivativeStep);
    if (!plus || !minus) {
        return tl::unexpected(!plus ? plus.error() : minus.error());
    }
    const SparseMatrix g = (*minus - *plus) / (2.0 * kDerivativeStep);

    // Estimates: subspace iteration on K0 phi = mu G phi.
    const auto p = static_cast<Eigen::Index>(std::min<Eigen::Index>(wanted + kSubspaceExtra, n));
    std::mt19937 random(2026);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    Eigen::MatrixXd x(n, p);
    for (Eigen::Index i = 0; i < x.size(); ++i) {
        x.data()[i] = uniform(random);
    }
    std::vector<std::pair<double, Eigen::VectorXd>> estimates;
    std::vector<double> last;
    for (int iteration = 0; iteration < kSubspaceIterations; ++iteration) {
        Eigen::MatrixXd y(n, p);
        for (Eigen::Index c = 0; c < p; ++c) {
            auto solved = base.solve(times(g, x.col(c)));
            if (!solved) {
                return tl::unexpected(solved.error());
            }
            y.col(c) = *solved;
        }
        Eigen::MatrixXd ky(n, p);
        Eigen::MatrixXd gy(n, p);
        for (Eigen::Index c = 0; c < p; ++c) {
            ky.col(c) = times(k0, y.col(c));
            gy.col(c) = times(g, y.col(c));
        }
        const Eigen::MatrixXd b = y.transpose() * ky;
        const Eigen::MatrixXd a = y.transpose() * gy;
        const Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXd> eigen(
            (a + a.transpose()) / 2.0, (b + b.transpose()) / 2.0);
        if (eigen.info() != Eigen::Success) {
            break;
        }
        x = y * eigen.eigenvectors();
        for (Eigen::Index c = 0; c < p; ++c) {
            x.col(c).normalize();
        }
        // mu = 1 / nu for the positive nu, in increasing mu.
        estimates.clear();
        for (Eigen::Index c = p - 1; c >= 0; --c) {
            const double nu = eigen.eigenvalues()(c);
            if (nu > 0.0) {
                estimates.emplace_back(1.0 / nu, x.col(c));
            }
        }
        std::vector<double> now;
        now.reserve(estimates.size());
        for (const auto& e : estimates) {
            now.push_back(e.first);
        }
        const auto settled = [&] {
            if (now.size() != last.size() || now.empty()) {
                return false;
            }
            const std::size_t check =
                std::min<std::size_t>(now.size(), static_cast<std::size_t>(wanted));
            for (std::size_t i = 0; i < check; ++i) {
                if (std::abs(now[i] - last[i]) > kEstimateSettled * now[i]) {
                    return false;
                }
            }
            return true;
        }();
        last = now;
        if (settled) {
            break;
        }
    }
    // The shift below the first estimate: nothing below it. Without
    // estimates (the bars buckle with their nodes held) only the count finds
    // the factors.
    double sigma = estimates.empty() ? 0.0 : (1.0 - kFirstShift) * estimates.front().first;
    std::unique_ptr<SparseCholesky> shifted;
    for (int halving = 0; !estimates.empty(); ++halving) {
        auto count = sturmCount(pencil, sigma, &shifted);
        if (!count) {
            return tl::unexpected(count.error());
        }
        if (*count == 0) {
            break;
        }
        if (halving == kBisections) {
            return makeError("The buckling search could not find a load factor below the first "
                             "buckling factor.");
        }
        // The estimate was high: further below.
        sigma = std::max(0.0, estimates.front().first *
                                  (1.0 - kFirstShift * std::pow(2.0, halving + 1)));
    }

    // Refine the estimates in order until `wanted` factors converge.
    std::vector<Factor> found;
    for (auto& [mu, start] : estimates) {
        if (static_cast<int>(found.size()) >= wanted) {
            break;
        }
        Factor factor{.lambda = mu, .x = start, .k = {}};
        // The shift of the count first: a step costs only products and a
        // solution. A factor too far from it for the steps gets a
        // factorization of its own just below its estimate.
        auto converged = refine(pencil, *shifted, k0, found, options.tolerance, factor.x,
                                factor.lambda, factor.k);
        if (!converged) {
            return tl::unexpected(converged.error());
        }
        if (!*converged) {
            std::unique_ptr<SparseCholesky> near;
            auto count = sturmCount(pencil, (1.0 - kModeShift) * mu, &near);
            if (!count) {
                return tl::unexpected(count.error());
            }
            factor = {.lambda = mu, .x = start, .k = {}};
            converged = refine(pencil, *near, k0, found, options.tolerance, factor.x, factor.lambda,
                               factor.k);
            if (!converged) {
                return tl::unexpected(converged.error());
            }
        }
        if (*converged && factor.lambda > 0.0) {
            found.push_back(std::move(factor));
        }
    }
    std::ranges::sort(found, {}, &Factor::lambda);

    // The guarantee (Wittrick-Williams): the count above the last factor
    // equals the factors found; the ones the vectors missed are bisected on
    // the count (design 008c §5.6).
    const auto below = [&found](double lambda) {
        return static_cast<std::int64_t>(
            std::ranges::count_if(found, [lambda](const Factor& f) { return f.lambda < lambda; }));
    };
    double high =
        found.empty() ? (estimates.empty() ? 1.0 : estimates.front().first) : found.back().lambda;
    high *= 1.0 + kCountMargin;
    auto total = sturmCount(pencil, high);
    if (!total) {
        return tl::unexpected(total.error());
    }
    for (int doubling = 0; *total < wanted && doubling < kDoublings; ++doubling) {
        // So far up that the matrix cannot be factorized: beyond the reach
        // of the search (REQ-008c-12).
        auto further = sturmCount(pencil, 2.0 * high);
        if (!further) {
            break;
        }
        high *= 2.0;
        total = further;
    }
    // Two refinements that met the same factor: keep one.
    while (below(high) > *total) {
        bool removed = false;
        for (std::size_t i = 1; i < found.size() && !removed; ++i) {
            const double gap = found[i].lambda - found[i - 1].lambda;
            if (gap <= kSameFactor * found[i].lambda &&
                std::abs(found[i].x.dot(found[i - 1].x)) > 0.99) {
                found.erase(found.begin() + static_cast<std::ptrdiff_t>(i));
                removed = true;
            }
        }
        if (!removed) {
            break;
        }
    }
    const std::int64_t target = std::min<std::int64_t>(*total, wanted);
    while (below(high) < target) {
        // The first lambda where the count passes the factors known below.
        double low = sigma;
        double top = high;
        for (int b = 0; b < kBisections && top - low > kBisectionPrecision * top; ++b) {
            const double middle = (low + top) / 2.0;
            auto count = sturmCount(pencil, middle);
            if (!count) {
                return tl::unexpected(count.error());
            }
            (*count > below(middle)) ? top = middle : low = middle;
        }
        Factor missing{.lambda = top, .x = {}, .k = {}};
        // Its vector, when it moves the nodes: from a shift just below it.
        std::unique_ptr<SparseCholesky> near;
        auto nearCount = sturmCount(pencil, top * (1.0 - 1e-6), &near);
        if (!nearCount) {
            return tl::unexpected(nearCount.error());
        }
        Eigen::VectorXd start = Eigen::VectorXd::NullaryExpr(n, [&] { return uniform(random); });
        double lambda = top;
        SparseMatrix k;
        auto converged = refine(pencil, *near, k0, found, options.tolerance, start, lambda, k);
        if (!converged) {
            return tl::unexpected(converged.error());
        }
        if (*converged && std::abs(lambda - top) <= 1e-6 * top) {
            missing = {.lambda = lambda, .x = std::move(start), .k = std::move(k)};
        } else {
            // Inside a bar: the nodes do not move. The bar is the one whose
            // count rises across the factor (the next one for a repeated
            // factor).
            missing.x = Eigen::VectorXd::Zero(n);
            std::vector<int> before;
            if (pencil.at(top * (1.0 - kInsideMargin))) {
                before = problem.elementNegativePivots();
            }
            if (auto after = pencil.at(top * (1.0 + kInsideMargin)); after) {
                missing.k = std::move(*after);
            }
            const std::vector<int>& now = problem.elementNegativePivots();
            const auto taken = std::ranges::count_if(found, [top](const Factor& f) {
                return f.inside >= 0 && std::abs(f.lambda - top) <= kSameFactor * top;
            });
            std::int64_t skip = taken;
            for (std::size_t e = 0; e < now.size() && e < before.size(); ++e) {
                if (now[e] > before[e] && skip-- == 0) {
                    missing.inside = static_cast<ElementIndex>(e);
                    break;
                }
            }
        }
        found.push_back(std::move(missing));
        std::ranges::sort(found, {}, &Factor::lambda);
    }
    if (static_cast<int>(found.size()) > wanted) {
        found.resize(static_cast<std::size_t>(wanted));
    }

    // The modes: normalized, and their shape along the bars.
    Eigen::Vector3d lowest = Eigen::Vector3d::Constant(1e300);
    Eigen::Vector3d highest = Eigen::Vector3d::Constant(-1e300);
    for (NodeIndex node = 0; node < problem.mesh().nodeCount(); ++node) {
        lowest = lowest.cwiseMin(problem.mesh().nodeCoordinates(node));
        highest = highest.cwiseMax(problem.mesh().nodeCoordinates(node));
    }
    const double size = std::max((highest - lowest).norm(), 1e-300);
    std::string inside;
    for (const Factor& factor : found) {
        if (factor.x.isZero()) {
            std::array<char, 32> text{};
            std::snprintf(text.data(), text.size(), "%.6g", factor.lambda);
            inside += (inside.empty() ? "" : ", ") + std::string(text.data());
        }
        Eigen::VectorXd values = problem.valuesOf(factor.x, false);
        double largest = 0.0;
        double largestRotation = 0.0;
        const DofMap& dofs = problem.dofs();
        for (DofIndex d = 0; d < dofs.size(); ++d) {
            const bool rotation = static_cast<int>(dofs.typeOf(d)) >= 3;
            double& top = rotation ? largestRotation : largest;
            if (std::abs(values[d]) > std::abs(top)) {
                top = values[d];
            }
        }
        // Translations that are only rounding (a mode inside the bars, with
        // the nodes held) do not count: by the rotation then.
        const double scale =
            std::abs(largest) > 1e-6 * std::abs(largestRotation) * size ? largest : largestRotation;
        if (scale != 0.0) {
            values /= scale;
        }
        std::vector<ReferenceProfile> scaled(reference.size());
        for (std::size_t e = 0; e < scaled.size(); ++e) {
            scaled[e] = reference[e].scaled(factor.lambda);
        }
        problem.setReference(scaled);
        auto shape = problem.recover(values, false);
        if (!shape) {
            return tl::unexpected(shape.error());
        }
        if (factor.inside >= 0) {
            shape->beams[static_cast<std::size_t>(factor.inside)] =
                problem.internalMode(factor.inside);
        }
        out.modes.push_back({.factor = factor.lambda,
                             .displacements = std::move(shape->displacements),
                             .rotations = std::move(shape->rotations),
                             .beams = std::move(shape->beams)});
    }
    if (!inside.empty()) {
        out.note =
            "The modes of the factors " + inside + " are inside the bars: their nodes do not move.";
    }
    if (static_cast<int>(out.modes.size()) < wanted) {
        out.note += out.note.empty() ? "" : " ";
        out.note += "Only " + std::to_string(out.modes.size()) + " of the " +
                    std::to_string(wanted) +
                    " buckling factors asked were found: the next ones are beyond the reach of "
                    "the search.";
    }
    return out;
}

} // namespace fem::core
