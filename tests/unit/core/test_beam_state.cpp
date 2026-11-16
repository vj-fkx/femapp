// REQ-008b-08, 13: the exact beam of design 008b §5.3.

#include <fem/core/beam_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>

using fem::core::matrixExponential;
using fem::core::StateMatrix;

namespace {

double relativeError(const StateMatrix& value, const StateMatrix& reference)
{
    return (value - reference).norm() / reference.norm();
}

} // namespace

// ADR 0006: e^A to the rounding error, for the sizes and norms of the beams.
TEST_CASE("the matrix exponential", "[core][beam][state]")
{
    SECTION("a nilpotent shift is a finite series")
    {
        const int n = 6;
        StateMatrix shift = StateMatrix::Zero(n, n);
        for (int i = 0; i + 1 < n; ++i) {
            shift(i, i + 1) = 2.0;
        }
        StateMatrix series = StateMatrix::Identity(n, n);
        StateMatrix power = StateMatrix::Identity(n, n);
        double factorial = 1.0;
        for (int k = 1; k < n; ++k) {
            power = power * shift;
            factorial *= k;
            series += power / factorial;
        }
        CHECK(relativeError(matrixExponential(shift), series) <= 1e-14);
    }

    SECTION("a diagonal matrix")
    {
        StateMatrix diagonal = StateMatrix::Zero(4, 4);
        diagonal.diagonal() << -3.0, 0.0, 0.5, 4.0;
        const StateMatrix e = matrixExponential(diagonal);
        for (int i = 0; i < 4; ++i) {
            CHECK(std::abs(e(i, i) - std::exp(diagonal(i, i))) <= 1e-14 * std::exp(diagonal(i, i)));
        }
        CHECK((e - StateMatrix(e.diagonal().asDiagonal())).norm() == 0.0);
    }

    SECTION("a skew matrix is a rotation (Rodrigues)")
    {
        const Eigen::Vector3d omega(0.3, -1.2, 2.0);
        StateMatrix skew = StateMatrix::Zero(3, 3);
        skew << 0.0, -omega.z(), omega.y(), omega.z(), 0.0, -omega.x(), -omega.y(), omega.x(), 0.0;
        const Eigen::Matrix3d rotation =
            Eigen::AngleAxisd(omega.norm(), omega.normalized()).toRotationMatrix();
        CHECK(relativeError(matrixExponential(skew), StateMatrix(rotation)) <= 1e-14);
    }

    SECTION("large norms, by scaling and squaring")
    {
        // [[a, b], [0, c]]: e^a, e^c and b (e^a - e^c) / (a - c).
        const double a = 20.0;
        const double c = -15.0;
        const double b = 3.0;
        StateMatrix m = StateMatrix::Zero(2, 2);
        m << a, b, 0.0, c;
        StateMatrix exact = StateMatrix::Zero(2, 2);
        exact << std::exp(a), b * (std::exp(a) - std::exp(c)) / (a - c), 0.0, std::exp(c);
        const StateMatrix e = matrixExponential(m);
        CHECK(std::abs(e(0, 0) - exact(0, 0)) <= 1e-13 * exact(0, 0));
        CHECK(std::abs(e(0, 1) - exact(0, 1)) <= 1e-13 * exact(0, 1));
        CHECK(std::abs(e(1, 1) - exact(1, 1)) <= 1e-13 * exact(1, 1));
        CHECK(e(1, 0) == 0.0);
    }

    SECTION("the largest size: e^A e^-A = I")
    {
        const int n = fem::core::kMaxStates;
        StateMatrix m = StateMatrix::Zero(n, n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                m(i, j) = std::sin(1.0 + i + 3.0 * j) * 0.4;
            }
        }
        const StateMatrix product = matrixExponential(m) * matrixExponential(-m);
        CHECK((product - StateMatrix::Identity(n, n)).norm() <= 1e-12);
    }
}
