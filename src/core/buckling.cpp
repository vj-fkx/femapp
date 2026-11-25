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

#include <fem/core/beam_state.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/stability_analysis.hpp>

#include <tbb/parallel_for.h>

#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
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
constexpr double kNoLinearPart = 1e-12;  // |G| / |K0| below it: no linear pencil
constexpr double kRealEstimate = 1e-6;   // |Im| / |Re| of an estimate taken as real
constexpr double kQuadraticWeight = 0.1; // mu^2 x.K2 x / x.K0 x above it: the quadratic pencil
constexpr double kBisectionPrecision = 1e-13;
constexpr double kCoarseBracket = 1e-3; // of a missed factor, before its refinement
constexpr int kInverseSteps = 3;        // inverse iteration before that refinement
constexpr double kNewtonBracket = 1e-7; // about Newton's lambda, checked by the count
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
    Pencil(StaticProblem& problem, std::vector<ReferenceProfile> reference,
           const RunControl& control)
        : m_problem(problem)
        , m_reference(std::move(reference))
        , m_control(control)
    {}

    // REQ-011-08, 09: one step of the search (a subspace iteration, a Sturm
    // count): reports it as an Iteration and stops at a cancellation.
    [[nodiscard]] Expected<void> step()
    {
        ++m_steps;
        m_control.report({.stage = RunStage::Iteration, .fraction = -1.0, .iteration = m_steps});
        if (m_control.cancelled()) {
            return makeCancelled();
        }
        return {};
    }

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
    const RunControl& m_control;
    int m_steps = 0;
};

// J(lambda): the factors below lambda (design 008c §5.4); the factorization
// is kept in `keep` when given. A zero pivot moves lambda a little below:
// 1e-8, then 1e-7 and 1e-6 (feature 008d, T23: a pivot is zero relative to
// the largest one, so stiff members widen the band where the matrix counts
// as singular about a factor; the factor there is not below lambda anyway).
Expected<std::int64_t> sturmCount(Pencil& pencil, double lambda,
                                  std::unique_ptr<SparseCholesky>* keep = nullptr)
{
    if (auto going = pencil.step(); !going) {
        return tl::unexpected(going.error());
    }
    const double start = lambda;
    double step = 1e-8;
    for (int attempt = 0; attempt < 4; ++attempt) {
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
        lambda = start * (1.0 - step);
        step *= 10.0;
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

Expected<BucklingResult> buckling(const Model& model, const BucklingOptions& options)
{
    // REQ-011-08, 09: the control is checked between the stages and at each
    // step of the search; the assemblies check it by blocks of elements.
    const RunControl& control = options.base.control;
    if (control.cancelled()) {
        return makeCancelled();
    }
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
    control.report({.stage = RunStage::Factorization, .fraction = -1.0, .iteration = 0});
    if (control.cancelled()) {
        return makeCancelled();
    }
    SparseCholesky base;
    if (auto factorized = base.factorize(k0); !factorized) {
        const auto eq = base.singularEquation();
        if (!eq) {
            return tl::unexpected(factorized.error());
        }
        return makeError("The structure is a mechanism: " + problem.equationLabel(*eq) +
                         " can move without resistance (a support may be missing).");
    }
    control.report({.stage = RunStage::Solution, .fraction = -1.0, .iteration = 0});
    if (control.cancelled()) {
        return makeCancelled();
    }
    auto linear = base.solve(system->rhs);
    if (!linear) {
        return tl::unexpected(linear.error());
    }
    std::vector<ReferenceProfile> reference = problem.profilesOf(problem.valuesOf(*linear));
    // REQ-008c-12, 008d-16: no compression, no bending moment and no
    // torque, no buckling (a moment can turn a beam out of its plane, a
    // torque bend a shaft: Greenhill).
    const auto unstable = [](const ReferenceProfile& p) {
        if (p.largestCompression() > 0.0) {
            return true;
        }
        for (int i = 0; i < p.count; ++i) {
            const ReferencePoint& r = p.values.at(static_cast<std::size_t>(i));
            if (r.my != 0.0 || r.mz != 0.0 || r.t != 0.0) {
                return true;
            }
        }
        return false;
    };
    if (std::ranges::none_of(reference, unstable)) {
        return makeError(
            "No bar is compressed, bent or twisted by these loads: there is no buckling factor.");
    }
    Pencil pencil(problem, reference, control);
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
    // Feature 008d (T18b, ADR 0007 addendum): with the flexural-torsional
    // terms K(lambda) is far from linear in lambda (lateral torsion goes
    // with lambda^2: the moments in the twisted section), and the linear
    // pencil misses the factors by tens of times. The estimates come from
    // the quadratic one, K0 + lambda K1 + lambda^2 K2, with K1 = -G and K2
    // first from a third assembly at 0 (refitted below).
    auto centre = pencil.at(0.0);
    if (!centre) {
        return tl::unexpected(centre.error());
    }
    SparseMatrix k2 = (*plus + *minus - 2.0 * *centre) / (2.0 * kDerivativeStep * kDerivativeStep);
    const bool linearized = g.norm() > kNoLinearPart * k0.norm();
    const bool quadratic = k2.norm() > kNoLinearPart * k0.norm();

    const auto p = static_cast<Eigen::Index>(std::min<Eigen::Index>(wanted + kSubspaceExtra, n));
    std::mt19937 random(2026);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    Eigen::MatrixXd x(n, p);
    for (Eigen::Index i = 0; i < x.size(); ++i) {
        x.data()[i] = uniform(random);
    }
    std::vector<std::pair<double, Eigen::VectorXd>> estimates;
    // Whether a list of estimates settled since the last iteration.
    const auto settledSince = [wanted](const std::vector<std::pair<double, Eigen::VectorXd>>& now,
                                       std::vector<double>& last) {
        bool settled = now.size() == last.size() && !now.empty();
        const std::size_t check =
            std::min<std::size_t>(now.size(), static_cast<std::size_t>(wanted));
        for (std::size_t i = 0; settled && i < check; ++i) {
            settled = std::abs(now[i].first - last[i]) <= kEstimateSettled * now[i].first;
        }
        last.clear();
        for (const auto& e : now) {
            last.push_back(e.first);
        }
        return settled;
    };

    // Estimates: subspace iteration on K0 phi = mu G phi (design 008c §5.6).
    std::vector<double> last;
    for (int iteration = 0; linearized && iteration < kSubspaceIterations; ++iteration) {
        if (auto going = pencil.step(); !going) {
            return tl::unexpected(going.error());
        }
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
        if (settledSince(estimates, last)) {
            break;
        }
    }

    // Feature 008d (T18b, ADR 0007 addendum): where the lambda^2 part of
    // K(lambda) weighs on the first estimate (lateral torsion: the moments in
    // the twisted section), the linear pencil misses the factors by tens of
    // times. The estimates then come from the quadratic pencil, Rayleigh-Ritz
    // on the basis of K0^-1 G x and K0^-1 K2 x, its companion form solved,
    // each real root's vector the null vector of the reduced matrix.
    bool useQuadratic = quadratic && estimates.empty();
    if (quadratic && !estimates.empty()) {
        const double mu = estimates.front().first;
        const Eigen::VectorXd& v = estimates.front().second;
        const double weight = mu * mu * std::abs(v.dot(times(k2, v)));
        useQuadratic = weight > kQuadraticWeight * std::abs(v.dot(times(k0, v)));
    }
    last.clear();
    for (int iteration = 0; useQuadratic && iteration < kSubspaceIterations; ++iteration) {
        if (auto going = pencil.step(); !going) {
            return tl::unexpected(going.error());
        }
        const Eigen::Index columns = std::min<Eigen::Index>(2 * p, n);
        Eigen::MatrixXd y(n, columns);
        for (Eigen::Index c = 0; c < columns; ++c) {
            const Eigen::VectorXd v = c < p ? times(g, x.col(c)) : times(k2, x.col(c - p));
            auto solved = base.solve(v);
            if (!solved) {
                return tl::unexpected(solved.error());
            }
            y.col(c) = *solved;
        }
        const Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(y);
        const Eigen::Index rank = qr.rank();
        if (rank == 0) {
            break;
        }
        const Eigen::MatrixXd q = Eigen::MatrixXd(qr.householderQ()).leftCols(rank);
        Eigen::MatrixXd a0(rank, rank);
        Eigen::MatrixXd a1(rank, rank);
        Eigen::MatrixXd a2(rank, rank);
        for (Eigen::Index c = 0; c < rank; ++c) {
            a0.col(c) = q.transpose() * times(k0, q.col(c));
            a1.col(c) = -(q.transpose() * times(g, q.col(c)));
            a2.col(c) = q.transpose() * times(k2, q.col(c));
        }
        // (A0 + lambda A1 + lambda^2 A2) v = 0 as [0 I; -A0 -A1] w =
        // lambda [I 0; 0 A2] w with w = (v, lambda v).
        const Eigen::Index m = 2 * rank;
        Eigen::MatrixXd left = Eigen::MatrixXd::Zero(m, m);
        Eigen::MatrixXd right = Eigen::MatrixXd::Zero(m, m);
        left.topRightCorner(rank, rank).setIdentity();
        left.bottomLeftCorner(rank, rank) = -a0;
        left.bottomRightCorner(rank, rank) = -a1;
        right.topLeftCorner(rank, rank).setIdentity();
        right.bottomRightCorner(rank, rank) = a2;
        const Eigen::GeneralizedEigenSolver<Eigen::MatrixXd> eigen(left, right, false);
        if (eigen.info() != Eigen::Success) {
            break;
        }
        std::vector<std::pair<double, Eigen::VectorXd>> now;
        for (Eigen::Index i = 0; i < m; ++i) {
            const double beta = eigen.betas()(i);
            if (std::abs(beta) <= 1e-300) {
                continue; // infinite
            }
            const std::complex<double> lambda = eigen.alphas()(i) / beta;
            if (!(lambda.real() > 0.0) ||
                std::abs(lambda.imag()) > kRealEstimate * std::abs(lambda.real())) {
                continue;
            }
            const double l = lambda.real();
            // The reduced matrix is symmetric: its null vector is the
            // eigenvector of the eigenvalue nearest zero.
            const Eigen::MatrixXd reduced = a0 + l * a1 + l * l * a2;
            const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> null(
                (reduced + reduced.transpose()) / 2.0);
            Eigen::Index nearest = 0;
            null.eigenvalues().cwiseAbs().minCoeff(&nearest);
            Eigen::VectorXd mode = q * null.eigenvectors().col(nearest);
            mode.normalize();
            now.emplace_back(l, std::move(mode));
        }
        std::ranges::sort(now, {}, &std::pair<double, Eigen::VectorXd>::first);
        if (static_cast<Eigen::Index>(now.size()) > p) {
            now.resize(static_cast<std::size_t>(p));
        }
        if (now.empty()) {
            break;
        }
        estimates = now;
        // K2 refitted so that the quadratic also passes through K(s) at the
        // first estimate s: exact at both ends of the range of the first
        // factor (the exact bars are not quadratic in lambda, and the
        // quadratic of the derivatives at 0 falls short of their factors).
        const double s = estimates.front().first;
        if (auto at = pencil.at(s); at) {
            k2 = (*at - *centre + s * g) / (s * s);
        }
        for (std::size_t c = 0; c < estimates.size(); ++c) {
            x.col(static_cast<Eigen::Index>(c)) = estimates[c].second;
        }
        if (settledSince(estimates, last)) {
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
            // Feature 008d (T90): an estimate so far up that the matrix
            // cannot be factorized below it (a spurious root of the pencils
            // when G barely acts on the nodes, the factors inside the bars:
            // 1.9e14 on Linux for an arch whose factor is about 1) is beyond
            // the reach of the search; only the count finds the factors.
            estimates.clear();
            shifted.reset();
            sigma = 0.0;
            break;
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
    // A count above the factors known below `high` means factors missed
    // below it: the lowest are bisected one by one; once `wanted` are
    // known, only the ones below the last wanted matter (feature 008d: many
    // torsional factors inside the bars can lie below a nodal one).
    const auto settle = [&]() -> Expected<void> {
        if (static_cast<int>(found.size()) >= wanted) {
            high = found[static_cast<std::size_t>(wanted) - 1].lambda * (1.0 + kCountMargin);
            auto recount = sturmCount(pencil, high);
            if (!recount) {
                return tl::unexpected(recount.error());
            }
            total = recount;
        }
        return {};
    };
    if (auto settled = settle(); !settled) {
        return tl::unexpected(settled.error());
    }
    // At most kMaxMissing bisected factors: a bar beyond its reach (the
    // count of kUnstableBar) is one factor, not a million.
    constexpr int kMaxMissing = 64;
    for (int missed = 0; below(high) < *total && missed < kMaxMissing; ++missed) {
        // The first lambda where the count passes the factors known below.
        // Feature 008d (T18b, ADR 0007 addendum): bisected first only to
        // kCoarseBracket, keeping the factorization at the low end and the
        // internal counts of the bars at both ends.
        double low = sigma;
        double top = high;
        std::unique_ptr<SparseCholesky> atLow;
        std::vector<int> lowPivots;
        std::vector<int> topPivots;
        const auto bisect = [&](double width) -> Expected<void> {
            for (int b = 0; b < kBisections && top - low > width * top; ++b) {
                const double middle = (low + top) / 2.0;
                std::unique_ptr<SparseCholesky> kept;
                auto count = sturmCount(pencil, middle, &kept);
                if (!count) {
                    return tl::unexpected(count.error());
                }
                if (*count > below(middle)) {
                    top = middle;
                    topPivots = problem.elementNegativePivots();
                } else {
                    low = middle;
                    lowPivots = problem.elementNegativePivots();
                    atLow = std::move(kept);
                }
            }
            return {};
        };
        if (auto coarse = bisect(kCoarseBracket); !coarse) {
            return tl::unexpected(coarse.error());
        }
        std::vector<Factor> fresh;

        // A factor that moves the nodes: inverse iteration from the low end,
        // kept when it lands in the bracket and is not one already found.
        if (!atLow) {
            auto count = sturmCount(pencil, low, &atLow);
            if (!count) {
                return tl::unexpected(count.error());
            }
            lowPivots = problem.elementNegativePivots();
        }
        {
            // Its vector first by inverse iteration at the low end, where the
            // matrix is nearly singular in that mode: Newton from a random
            // vector throws lambda far away.
            Eigen::VectorXd start =
                Eigen::VectorXd::NullaryExpr(n, [&] { return uniform(random); });
            for (int step = 0; step < kInverseSteps; ++step) {
                auto solved = atLow->solve(start);
                if (!solved) {
                    return tl::unexpected(solved.error());
                }
                start = solved->normalized();
            }
            double lambda = (low + top) / 2.0;
            SparseMatrix k;
            auto converged = refine(pencil, *atLow, k0, found, options.tolerance, start, lambda, k);
            if (!converged) {
                return tl::unexpected(converged.error());
            }
            const bool inside =
                lambda >= low * (1.0 - kInsideMargin) && lambda <= top * (1.0 + kInsideMargin);
            const bool known = std::ranges::any_of(found, [&](const Factor& f) {
                return std::abs(f.lambda - lambda) <= kSameFactor * lambda &&
                       std::abs(f.x.dot(start)) > 0.99;
            });
            if (*converged && inside && !known) {
                fresh.push_back({.lambda = lambda, .x = std::move(start), .k = std::move(k)});
            } else if (inside) {
                // Newton's lambda, short of the residual (its rounding in
                // some modes): the count checks it, and the bracket closes
                // about it.
                const double around = kNewtonBracket * lambda;
                auto under = sturmCount(pencil, lambda - around);
                if (!under) {
                    return tl::unexpected(under.error());
                }
                std::vector<int> underPivots = problem.elementNegativePivots();
                auto over = sturmCount(pencil, lambda + around);
                if (!over) {
                    return tl::unexpected(over.error());
                }
                if (*under <= below(lambda - around) && *over > below(lambda + around)) {
                    low = lambda - around;
                    top = lambda + around;
                    lowPivots = std::move(underPivots);
                    topPivots = problem.elementNegativePivots();
                }
            }
        }

        // A factor inside a bar, its nodes held: the bars whose internal
        // count rises in the bracket are bisected on their own count (one
        // element at a time, in parallel), and the lowest, with the bars
        // alike at the same factor, confirmed by the global count.
        if (fresh.empty()) {
            if (topPivots.empty() && pencil.at(top)) {
                topPivots = problem.elementNegativePivots();
            }
            struct Rise
            {
                double lambda = 0.0;
                ElementIndex element = -1;
                int level = 0;
            };
            std::vector<Rise> rises;
            for (std::size_t e = 0; e < topPivots.size() && e < lowPivots.size(); ++e) {
                if (topPivots[e] > lowPivots[e] && topPivots[e] < kUnstableBar) {
                    rises.push_back({.lambda = top,
                                     .element = static_cast<ElementIndex>(e),
                                     .level = lowPivots[e] + 1});
                }
            }
            tbb::parallel_for(std::size_t{0}, rises.size(), [&](std::size_t i) {
                Rise& rise = rises[i];
                const ReferenceProfile& base = reference[static_cast<std::size_t>(rise.element)];
                double under = low;
                for (int b = 0;
                     b < kBisections && rise.lambda - under > kBisectionPrecision * rise.lambda;
                     ++b) {
                    const double middle = (under + rise.lambda) / 2.0;
                    (problem.internalCount(rise.element, base.scaled(middle)) >= rise.level)
                        ? rise.lambda = middle
                        : under = middle;
                }
            });
            std::ranges::sort(rises, {}, &Rise::lambda);
            if (!rises.empty()) {
                const double lambda = rises.front().lambda;
                auto after = sturmCount(pencil, lambda * (1.0 + kInsideMargin));
                auto before = sturmCount(pencil, lambda * (1.0 - kInsideMargin));
                if (!after || !before) {
                    return tl::unexpected(!after ? after.error() : before.error());
                }
                SparseMatrix k;
                if (auto matrix = pencil.at(lambda * (1.0 + kInsideMargin)); matrix) {
                    k = std::move(*matrix);
                }
                const auto there = std::ranges::count_if(found, [lambda](const Factor& f) {
                    return std::abs(f.lambda - lambda) <= kSameFactor * lambda;
                });
                const auto same = std::ranges::count_if(rises, [lambda](const Rise& r) {
                    return std::abs(r.lambda - lambda) <= kSameFactor * lambda;
                });
                const auto count = std::min<std::int64_t>(*after - *before - there, same);
                for (std::int64_t i = 0; i < count; ++i) {
                    const Rise& rise = rises[static_cast<std::size_t>(i)];
                    fresh.push_back({.lambda = rise.lambda,
                                     .x = Eigen::VectorXd::Zero(n),
                                     .k = k,
                                     .inside = rise.element});
                }
            }
        }

        // Otherwise the bisection on the global count goes to the end.
        if (fresh.empty()) {
            if (auto fine = bisect(kBisectionPrecision); !fine) {
                return tl::unexpected(fine.error());
            }
            bool stop = false;
            Factor missing{.lambda = top, .x = {}, .k = {}};
            // Its vector, when it moves the nodes: from a shift just below it.
            std::unique_ptr<SparseCholesky> near;
            auto nearCount = sturmCount(pencil, top * (1.0 - 1e-6), &near);
            if (!nearCount) {
                return tl::unexpected(nearCount.error());
            }
            Eigen::VectorXd start =
                Eigen::VectorXd::NullaryExpr(n, [&] { return uniform(random); });
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
            // A bar beyond its reach (the count jumps by kUnstableBar there) is
            // one factor: nothing more to find at it.
            if (missing.x.isZero()) {
                auto above = sturmCount(pencil, top * (1.0 + kInsideMargin));
                auto under = sturmCount(pencil, top * (1.0 - kInsideMargin));
                if (above && under && *above - *under >= kUnstableBar) {
                    const bool again = std::ranges::any_of(found, [&missing](const Factor& f) {
                        return std::abs(f.lambda - missing.lambda) <= kSameFactor * missing.lambda;
                    });
                    if (again) {
                        stop = true;
                    }
                }
            }
            if (stop) {
                break;
            }
            fresh.push_back(std::move(missing));
        }
        for (Factor& f : fresh) {
            found.push_back(std::move(f));
        }
        std::ranges::sort(found, {}, &Factor::lambda);
        if (static_cast<int>(found.size()) > wanted) {
            found.resize(static_cast<std::size_t>(wanted));
        }
        if (auto settled = settle(); !settled) {
            return tl::unexpected(settled.error());
        }
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
            // The translations and the rotations only: the warping (a rate
            // of twist, REQ-008d-15) follows them.
            const auto type = dofs.typeOf(d);
            if (type > DofType::RZ) {
                continue;
            }
            const bool rotation = type >= DofType::RX;
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
                             .warping = std::move(shape->warping),
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

} // namespace

Expected<BucklingResult> runLinearBuckling(const Model& model, const BucklingOptions& options)
{
    // REQ-011-08: an error met after a cancellation (some steps of the search
    // step around a failed count) is the cancellation.
    auto result = buckling(model, options);
    if (!result && options.base.control.cancelled()) {
        return makeCancelled();
    }
    return result;
}

} // namespace fem::core
