#include <fem/core/linear_solver.hpp>

#include <Eigen/OrderingMethods>
#include <Eigen/SparseCholesky>

namespace fem::core {

namespace {

constexpr double kNegligiblePivot = 1e-14;

} // namespace

struct SparseCholesky::Impl
{
    Eigen::SimplicialLDLT<SparseMatrix, Eigen::Lower, Eigen::AMDOrdering<std::int32_t>> ldlt;
    bool factorized = false;
    std::optional<std::int32_t> singular;

    // The first pivot, in elimination order, that is not clearly positive,
    // taken back to the numbering of the input. Eigen stores the failing
    // pivot before it stops, so the entries after it are never read.
    void findSingular(double largest)
    {
        const Eigen::VectorXd& d = ldlt.vectorD();
        for (Eigen::Index k = 0; k < d.size(); ++k) {
            if (!(d[k] > kNegligiblePivot * largest)) {
                singular = ldlt.permutationPinv().indices()[k];
                return;
            }
        }
    }
};

SparseCholesky::SparseCholesky()
    : m_impl(std::make_unique<Impl>())
{}

SparseCholesky::~SparseCholesky() = default;

Expected<void> SparseCholesky::factorize(const SparseMatrix& lowerTriangle)
{
    m_impl->factorized = false;
    m_impl->singular.reset();
    if (lowerTriangle.rows() == 0) {
        m_impl->factorized = true;
        return {};
    }
    m_impl->ldlt.compute(lowerTriangle);
    const auto singular = [] {
        return makeError("The system matrix is singular or not positive definite "
                         "(a support may be missing).");
    };
    if (m_impl->ldlt.info() != Eigen::Success) {
        // The reference for a negligible pivot: the largest diagonal entry.
        m_impl->findSingular(lowerTriangle.diagonal().cwiseAbs().maxCoeff());
        return singular();
    }
    // REQ-006-16: pivots that are not clearly positive.
    const Eigen::VectorXd& d = m_impl->ldlt.vectorD();
    const double largest = d.cwiseAbs().maxCoeff();
    if (!(d.minCoeff() > kNegligiblePivot * largest)) {
        m_impl->findSingular(largest);
        return singular();
    }
    m_impl->factorized = true;
    return {};
}

std::optional<std::int32_t> SparseCholesky::singularEquation() const
{
    return m_impl->singular;
}

Expected<Eigen::VectorXd> SparseCholesky::solve(const Eigen::VectorXd& rhs) const
{
    if (!m_impl->factorized) {
        return makeError("The system has not been factorized.");
    }
    if (rhs.size() == 0) {
        return Eigen::VectorXd();
    }
    Eigen::VectorXd x = m_impl->ldlt.solve(rhs);
    if (m_impl->ldlt.info() != Eigen::Success) {
        return makeError("The linear system could not be solved.");
    }
    return x;
}

} // namespace fem::core
