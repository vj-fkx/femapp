#pragma once

#include <fem/core/assembler.hpp>
#include <fem/core/expected.hpp>

#include <Eigen/Core>
#include <cstdint>
#include <memory>
#include <optional>

namespace fem::core {

// REQ-006-15: a solver for symmetric positive definite sparse systems, given
// by their lower triangle. Other solvers (iterative, external) plug in here.
class LinearSolver
{
public:
    LinearSolver() = default;
    virtual ~LinearSolver() = default;
    LinearSolver(const LinearSolver&) = delete;
    LinearSolver& operator=(const LinearSolver&) = delete;
    LinearSolver(LinearSolver&&) = delete;
    LinearSolver& operator=(LinearSolver&&) = delete;

    [[nodiscard]] virtual Expected<void> factorize(const SparseMatrix& lowerTriangle) = 0;
    [[nodiscard]] virtual Expected<Eigen::VectorXd> solve(const Eigen::VectorXd& rhs) const = 0;
};

// REQ-006-15, 16: sparse LDL^T (Eigen SimplicialLDLT, AMD ordering). A zero,
// negative or negligible pivot (below 1e-14 of the largest) means a singular
// or indefinite matrix, such as a mechanism.
class SparseCholesky final : public LinearSolver
{
public:
    SparseCholesky();
    ~SparseCholesky() override;
    SparseCholesky(const SparseCholesky&) = delete;
    SparseCholesky& operator=(const SparseCholesky&) = delete;
    SparseCholesky(SparseCholesky&&) = delete;
    SparseCholesky& operator=(SparseCholesky&&) = delete;

    [[nodiscard]] Expected<void> factorize(const SparseMatrix& lowerTriangle) override;
    [[nodiscard]] Expected<Eigen::VectorXd> solve(const Eigen::VectorXd& rhs) const override;

    // REQ-007-15: after a failed factorize(), the first equation (in the
    // numbering of the input matrix) whose pivot was not clearly positive;
    // after a failed factorizeIndefinite(), the first negligible pivot.
    [[nodiscard]] std::optional<std::int32_t> singularEquation() const;

    // REQ-008c-08, 10 (ADR 0007): LDL^T of a symmetric matrix that may be
    // indefinite, without pivoting: it fails only on a negligible pivot
    // (below 1e-14 of the largest). The symbolic analysis is kept while the
    // pattern of the matrix stays the same.
    [[nodiscard]] Expected<void> factorizeIndefinite(const SparseMatrix& lowerTriangle);

    // The negative pivots of the last successful factorization: by
    // Sylvester's law, the number of negative eigenvalues of the matrix.
    [[nodiscard]] std::int64_t negativePivots() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::core
