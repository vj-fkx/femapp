// REQ-008c-01, 05 to 08: the second-order static analysis (design 008c
// §5.5): the linear solution gives the first axial forces; each iteration
// assembles the bars with the axial forces of the last one and solves by
// conjugate gradients preconditioned with a factorization kept from before
// (one costs almost a whole linear analysis, T03); Anderson accelerates the
// iteration on the axial forces; a final factorization checks by its
// inertia that the structure is stable (Sturm count, ADR 0007).

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

// The values of all profiles in one vector, and back.
struct Flat
{
    std::vector<int> count; // per element
};

Eigen::VectorXd flatten(const std::vector<AxialProfile>& profiles, Flat& flat)
{
    flat.count.resize(profiles.size());
    Eigen::Index size = 0;
    for (std::size_t e = 0; e < profiles.size(); ++e) {
        flat.count[e] = profiles[e].count;
        size += profiles[e].count;
    }
    Eigen::VectorXd x(size);
    Eigen::Index i = 0;
    for (const AxialProfile& profile : profiles) {
        for (int k = 0; k < profile.count; ++k) {
            x(i++) = profile.values.at(static_cast<std::size_t>(k));
        }
    }
    return x;
}

std::vector<AxialProfile> unflatten(const Eigen::VectorXd& x, const Flat& flat)
{
    std::vector<AxialProfile> profiles(flat.count.size());
    Eigen::Index i = 0;
    for (std::size_t e = 0; e < profiles.size(); ++e) {
        profiles[e].count = flat.count[e];
        for (int k = 0; k < flat.count[e]; ++k) {
            profiles[e].values.at(static_cast<std::size_t>(k)) = x(i++);
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

} // namespace

Expected<SecondOrderResult> runSecondOrderStatic(const Model& model,
                                                 const SecondOrderOptions& options)
{
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
    auto preconditioner = std::make_unique<SparseCholesky>();
    if (auto factorized = preconditioner->factorize(system->matrix); !factorized) {
        const auto eq = preconditioner->singularEquation();
        if (!eq) {
            return tl::unexpected(factorized.error());
        }
        return makeError("The structure is a mechanism: " + problem.equationLabel(*eq) +
                         " can move without resistance (a support may be missing).");
    }
    auto linear = preconditioner->solve(system->rhs);
    if (!linear) {
        return tl::unexpected(linear.error());
    }
    Eigen::VectorXd u = *linear;
    Flat flat;
    Eigen::VectorXd x = flatten(problem.profilesOf(problem.valuesOf(u)), flat);
    Eigen::VectorXd stableX = x;

    // REQ-008c-08: the instability, at a node and direction or in a bar.
    const auto unstable = [&](int iteration, const SparseCholesky* factorization) {
        std::string where;
        if (factorization != nullptr) {
            auto eq = factorization->firstNegativeEquation();
            if (!eq) {
                eq = factorization->singularEquation();
            }
            if (eq) {
                where = "at " + problem.equationLabel(*eq);
            }
        }
        if (where.empty() && problem.unstableElement() >= 0) {
            where = "inside the element " +
                    std::to_string(problem.mesh().elementId(problem.unstableElement()));
        }
        return makeError("The structure is unstable under these loads: the second-order "
                         "stiffness lost its stability at iteration " +
                         std::to_string(iteration) + (where.empty() ? "" : " (" + where + ")") +
                         ".");
    };

    Anderson anderson;
    int halvings = 0;
    double change = 0.0;
    for (int k = 1; k <= options.maxIterations; ++k) {
        problem.setAxialForces(unflatten(x, flat));
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
                // A stalled preconditioner: a new one at these axial forces.
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
            // stable axial forces, then decide by a factorization.
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
            // REQ-008c-08: stable at the converged axial forces (Sturm).
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

} // namespace fem::core
