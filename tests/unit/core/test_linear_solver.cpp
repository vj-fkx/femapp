// REQ-008c-08, 10: the indefinite factorization of the direct solver and
// its inertia (ADR 0007).

#include <fem/core/linear_solver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

using fem::core::SparseMatrix;

SparseMatrix diagonal(const std::vector<double>& values)
{
    SparseMatrix m(static_cast<Eigen::Index>(values.size()),
                   static_cast<Eigen::Index>(values.size()));
    std::vector<Eigen::Triplet<double, std::int32_t>> entries;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto k = static_cast<std::int32_t>(i);
        entries.emplace_back(k, k, values[i]);
    }
    m.setFromTriplets(entries.begin(), entries.end());
    return m;
}

// The lower triangle of tridiag(-1, 2, -1) - shift I, of size n: its
// eigenvalues are 2 - 2 cos(k pi / (n + 1)) - shift, k = 1..n.
SparseMatrix shiftedLaplacian(int n, double shift)
{
    SparseMatrix m(n, n);
    std::vector<Eigen::Triplet<double, std::int32_t>> entries;
    for (int i = 0; i < n; ++i) {
        entries.emplace_back(i, i, 2.0 - shift);
        if (i + 1 < n) {
            entries.emplace_back(i + 1, i, -1.0);
        }
    }
    m.setFromTriplets(entries.begin(), entries.end());
    return m;
}

std::int64_t eigenvaluesBelow(int n, double shift)
{
    std::int64_t count = 0;
    for (int k = 1; k <= n; ++k) {
        if (2.0 - 2.0 * std::cos(k * std::numbers::pi / (n + 1)) < shift) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("the indefinite factorization gives the inertia", "[core][solver]")
{
    SECTION("a diagonal matrix")
    {
        fem::core::SparseCholesky solver;
        REQUIRE(solver.factorizeIndefinite(diagonal({2.0, -3.0, 4.0, -1.0, 5.0})).has_value());
        CHECK(solver.negativePivots() == 2);
        const auto x = solver.solve(Eigen::VectorXd::Ones(5));
        REQUIRE(x.has_value());
        CHECK(std::abs((*x)(1) + 1.0 / 3.0) < 1e-15);
    }
    SECTION("shifted Laplacians: the count of the eigenvalues below the shift")
    {
        const int n = 40;
        fem::core::SparseCholesky reused;
        // Not 1.0: without pivoting, its pivots run 1, 0, ... (a zero pivot
        // of a regular matrix, which the buckling search steps around).
        for (const double shift : {-0.5, 0.01, 0.3, 1.1, 2.5, 3.9, 4.5}) {
            const SparseMatrix m = shiftedLaplacian(n, shift);
            REQUIRE(reused.factorizeIndefinite(m).has_value());
            CHECK(reused.negativePivots() == eigenvaluesBelow(n, shift));
            // The same with a fresh symbolic analysis.
            fem::core::SparseCholesky fresh;
            REQUIRE(fresh.factorizeIndefinite(m).has_value());
            CHECK(fresh.negativePivots() == reused.negativePivots());
            // The solution of the indefinite system.
            const Eigen::VectorXd b = Eigen::VectorXd::LinSpaced(n, 1.0, 2.0);
            const auto x = reused.solve(b);
            REQUIRE(x.has_value());
            const SparseMatrix symmetric = m.selfadjointView<Eigen::Lower>();
            const Eigen::MatrixXd full(symmetric);
            CHECK((full * *x - b).norm() <= 1e-10 * b.norm() * (1.0 + x->norm()));
        }
    }
    SECTION("a zero pivot is refused and named")
    {
        fem::core::SparseCholesky solver;
        const auto result = solver.factorizeIndefinite(diagonal({1.0, -2.0, 0.0, 3.0}));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(solver.singularEquation().has_value());
        CHECK(solver.singularEquation().value_or(-1) == 2);
        CHECK_FALSE(solver.solve(Eigen::VectorXd::Ones(4)).has_value());
    }
    SECTION("the positive definite factorization still refuses indefinite matrices")
    {
        fem::core::SparseCholesky solver;
        REQUIRE(solver.factorizeIndefinite(diagonal({1.0, -2.0})).has_value());
        CHECK_FALSE(solver.factorize(diagonal({1.0, -2.0})).has_value());
        REQUIRE(solver.factorize(diagonal({1.0, 2.0})).has_value());
        CHECK(solver.negativePivots() == 0);
        // And the indefinite one analyses its pattern again afterwards.
        REQUIRE(solver.factorizeIndefinite(diagonal({-1.0, 2.0})).has_value());
        CHECK(solver.negativePivots() == 1);
    }
}
