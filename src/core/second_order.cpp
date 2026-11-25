// REQ-008c-01, 05 to 08: the second-order static analysis (design 008c
// §5.5): the linear solution gives the first forces of the bars (feature
// 008d: all six, design 008d §5.8); each iteration assembles the bars with
// the forces of the last one and solves by
// conjugate gradients preconditioned with a factorization kept from before
// (one costs almost a whole linear analysis, T03); Anderson accelerates the
// iteration on the forces; a final factorization checks by its
// inertia that the structure is stable (Sturm count, ADR 0007).

#include "detail/instability.hpp"
#include "detail/static_stages.hpp"

#include <fem/core/linear_solver.hpp>
#include <fem/core/stability_analysis.hpp>

#include <Eigen/QR>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fem::core {

namespace {

using detail::StaticProblem;

constexpr int kAndersonDepth = 5;
constexpr int kHalvings = 5;
constexpr int kGradientIterations = 100;
constexpr double kGradientTolerance = 1e-12;

// The values of all profiles in one vector, and back (feature 008d: the six
// forces of every point, and the forces at the start of a bar whose forces
// come from statics; the rest of each profile is kept as it was).
struct Flat
{
    std::vector<ReferenceProfile> shape; // per element
};

constexpr int kForces = 6;

void put(const ReferencePoint& point, Eigen::VectorXd& x, Eigen::Index& i)
{
    for (const double value : {point.n, point.vy, point.vz, point.t, point.my, point.mz}) {
        x(i++) = value;
    }
}

ReferencePoint take(const Eigen::VectorXd& x, Eigen::Index& i)
{
    ReferencePoint point;
    for (double* value : {&point.n, &point.vy, &point.vz, &point.t, &point.my, &point.mz}) {
        *value = x(i++);
    }
    return point;
}

Eigen::VectorXd flatten(const std::vector<ReferenceProfile>& profiles, Flat& flat)
{
    flat.shape = profiles;
    Eigen::Index size = 0;
    for (const ReferenceProfile& profile : profiles) {
        size += static_cast<Eigen::Index>(kForces) * (profile.count + (profile.statics ? 1 : 0));
    }
    Eigen::VectorXd x(size);
    Eigen::Index i = 0;
    for (const ReferenceProfile& profile : profiles) {
        for (int k = 0; k < profile.count; ++k) {
            put(profile.values.at(static_cast<std::size_t>(k)), x, i);
        }
        if (profile.statics) {
            put(profile.start, x, i);
        }
    }
    return x;
}

std::vector<ReferenceProfile> unflatten(const Eigen::VectorXd& x, const Flat& flat)
{
    std::vector<ReferenceProfile> profiles = flat.shape;
    Eigen::Index i = 0;
    for (ReferenceProfile& profile : profiles) {
        for (int k = 0; k < profile.count; ++k) {
            profile.values.at(static_cast<std::size_t>(k)) = take(x, i);
        }
        if (profile.statics) {
            profile.start = take(x, i);
        }
    }
    return profiles;
}

// Conjugate gradients on the system of `lower` (its lower triangle) from x,
// preconditioned by a factorization; stops at a relative residual of
// kGradientTolerance, or on a direction of non-positive curvature (the
// matrix is not positive definite).
struct Gradient
{
    bool converged = false;
    bool negativeCurvature = false;
};

Gradient conjugateGradient(const SparseMatrix& lower, const Eigen::VectorXd& b,
                           const SparseCholesky& preconditioner, Eigen::VectorXd& x)
{
    Gradient out;
    const double bNorm = b.norm();
    if (b.size() == 0 || bNorm == 0.0) {
        x = Eigen::VectorXd::Zero(b.size());
        out.converged = true;
        return out;
    }
    const auto product = [&lower](const Eigen::VectorXd& v) {
        return Eigen::VectorXd(lower.selfadjointView<Eigen::Lower>() * v);
    };
    Eigen::VectorXd r = b - product(x);
    auto z = preconditioner.solve(r);
    if (!z) {
        return out;
    }
    Eigen::VectorXd p = *z;
    double rz = r.dot(*z);
    for (int k = 0; k < kGradientIterations; ++k) {
        if (r.norm() <= kGradientTolerance * bNorm) {
            out.converged = true;
            return out;
        }
        const Eigen::VectorXd ap = product(p);
        const double curvature = p.dot(ap);
        if (!(curvature > 0.0)) {
            out.negativeCurvature = true;
            return out;
        }
        const double alpha = rz / curvature;
        x += alpha * p;
        r -= alpha * ap;
        z = preconditioner.solve(r);
        if (!z) {
            return out;
        }
        const double next = r.dot(*z);
        p = *z + (next / rz) * p;
        rz = next;
    }
    out.converged = r.norm() <= kGradientTolerance * bNorm;
    return out;
}

// Anderson acceleration (type II) of the fixed point x = g(x).
class Anderson
{
public:
    Eigen::VectorXd next(const Eigen::VectorXd& x, const Eigen::VectorXd& g)
    {
        const Eigen::VectorXd f = g - x;
        if (m_previousF && m_previousG) {
            m_df.emplace_back(f - *m_previousF);
            m_dg.emplace_back(g - *m_previousG);
            if (m_df.size() > kAndersonDepth) {
                m_df.erase(m_df.begin());
                m_dg.erase(m_dg.begin());
            }
        }
        m_previousF = f;
        m_previousG = g;
        if (m_df.empty()) {
            return g;
        }
        const auto columns = static_cast<Eigen::Index>(m_df.size());
        Eigen::MatrixXd df(f.size(), columns);
        Eigen::MatrixXd dg(f.size(), columns);
        for (Eigen::Index c = 0; c < columns; ++c) {
            df.col(c) = m_df[static_cast<std::size_t>(c)];
            dg.col(c) = m_dg[static_cast<std::size_t>(c)];
        }
        const Eigen::VectorXd gamma = df.colPivHouseholderQr().solve(f);
        return g - dg * gamma;
    }

    void reset()
    {
        m_df.clear();
        m_dg.clear();
        m_previousF.reset();
        m_previousG.reset();
    }

private:
    std::vector<Eigen::VectorXd> m_df;
    std::vector<Eigen::VectorXd> m_dg;
    std::optional<Eigen::VectorXd> m_previousF;
    std::optional<Eigen::VectorXd> m_previousG;
};

double relativeChange(const Eigen::VectorXd& now, const Eigen::VectorXd& before)
{
    const double size = now.norm();
    return size > 0.0 ? (now - before).norm() / size : (now - before).norm();
}

Expected<SecondOrderResult> secondOrder(const Model& model, const SecondOrderOptions& options)
{
    // REQ-011-08, 09: the control is checked between the stages and at each
    // iteration; the assemblies check it by blocks of elements.
    const RunControl& control = options.base.control;
    if (control.cancelled()) {
        return makeCancelled();
    }
    auto prepared = StaticProblem::prepare(model, options.base, true);
    if (!prepared) {
        return tl::unexpected(prepared.error());
    }
    StaticProblem& problem = **prepared;

    // The linear solution and the factorization that preconditions the rest.
    auto system = problem.assemble();
    if (!system) {
        return tl::unexpected(system.error());
    }
    if (auto stable = problem.checkDiagonal(system->matrix); !stable) {
        return tl::unexpected(stable.error());
    }
    control.report({.stage = RunStage::Factorization, .fraction = -1.0, .iteration = 0});
    if (control.cancelled()) {
        return makeCancelled();
    }
    auto preconditioner = std::make_unique<SparseCholesky>();
    if (auto factorized = preconditioner->factorize(system->matrix); !factorized) {
        return mechanismError(problem, system->matrix, *preconditioner, factorized.error());
    }
    control.report({.stage = RunStage::Solution, .fraction = -1.0, .iteration = 0});
    if (control.cancelled()) {
        return makeCancelled();
    }
    auto linear = preconditioner->solve(system->rhs);
    if (!linear) {
        return tl::unexpected(linear.error());
    }
    Eigen::VectorXd u = *linear;
    Flat flat;
    Eigen::VectorXd x = flatten(problem.profilesOf(problem.valuesOf(u)), flat);
    Eigen::VectorXd stableX = x;

    // REQ-008c-08, REQ-011-18: the instability, where it is.
    const auto unstable = [&](int iteration, const SparseCholesky* factorization) {
        return secondOrderError(problem, system->matrix, iteration, factorization);
    };

    Anderson anderson;
    int halvings = 0;
    double change = 0.0;
    for (int k = 1; k <= options.maxIterations; ++k) {
        control.report({.stage = RunStage::Iteration,
                        .fraction = static_cast<double>(k) / options.maxIterations,
                        .iteration = k});
        if (control.cancelled()) {
            return makeCancelled();
        }
        problem.setReference(unflatten(x, flat));
        system = problem.assemble();
        if (!system) {
            return tl::unexpected(system.error());
        }
        Eigen::VectorXd next = u;
        bool suspect = problem.negativePivots() > 0;
        if (!suspect) {
            const Gradient gradient =
                conjugateGradient(system->matrix, system->rhs, *preconditioner, next);
            suspect = gradient.negativeCurvature;
            if (!gradient.converged && !suspect) {
                // A stalled preconditioner: a new one at these forces.
                auto fresh = std::make_unique<SparseCholesky>();
                if (!fresh->factorizeIndefinite(system->matrix) || fresh->negativePivots() > 0) {
                    suspect = true;
                } else {
                    preconditioner = std::move(fresh);
                    auto solved = preconditioner->solve(system->rhs);
                    if (!solved) {
                        return tl::unexpected(solved.error());
                    }
                    next = *solved;
                }
            }
        }
        if (suspect) {
            // Perhaps an accelerated step overshot: back towards the last
            // stable forces, then decide by a factorization.
            if (halvings < kHalvings) {
                ++halvings;
                x = 0.5 * (x + stableX);
                anderson.reset();
                continue;
            }
            auto check = std::make_unique<SparseCholesky>();
            if (!check->factorizeIndefinite(system->matrix) || check->negativePivots() > 0 ||
                problem.negativePivots() > 0) {
                return unstable(k, check.get());
            }
            preconditioner = std::move(check);
            auto solved = preconditioner->solve(system->rhs);
            if (!solved) {
                return tl::unexpected(solved.error());
            }
            next = *solved;
        }
        halvings = 0;
        stableX = x;
        const Eigen::VectorXd g = flatten(problem.profilesOf(problem.valuesOf(next)), flat);
        change = std::max(relativeChange(next, u), relativeChange(g, x));
        u = next;
        if (change <= options.tolerance) {
            // REQ-008c-08: stable at the converged forces (Sturm).
            SparseCholesky check;
            if (!check.factorizeIndefinite(system->matrix) || check.negativePivots() > 0 ||
                problem.negativePivots() > 0) {
                return unstable(k, &check);
            }
            auto result = problem.recover(problem.valuesOf(u));
            if (!result) {
                return tl::unexpected(result.error());
            }
            return SecondOrderResult{
                .result = std::move(*result), .iterations = k, .change = change};
        }
        x = anderson.next(x, g);
    }
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.3g", change);
    return makeError("The second-order analysis did not converge in " +
                     std::to_string(options.maxIterations) + " iterations (last change " +
                     text.data() + ").");
}

} // namespace

Expected<SecondOrderResult> runSecondOrderStatic(const Model& model,
                                                 const SecondOrderOptions& options)
{
    // REQ-011-08: an error met after a cancellation is the cancellation.
    auto result = secondOrder(model, options);
    if (!result && options.base.control.cancelled()) {
        return makeCancelled();
    }
    return result;
}

} // namespace fem::core
