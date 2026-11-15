#include <fem/core/shape_functions.hpp>

#include <Eigen/LU>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace fem::core {

namespace {

// A monomial xi^a eta^b zeta^c.
using Exponents = std::array<std::uint8_t, 3>;

// Polynomial space of a type (design 006 §5.1).
std::vector<Exponents> spaceOf(ElementType type)
{
    std::vector<Exponents> space;
    const auto add = [&space](int a, int b, int c) {
        space.push_back({static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                         static_cast<std::uint8_t>(c)});
    };
    // Complete polynomials of total degree <= p in `dims` variables.
    const auto complete = [&](int p, int dims) {
        for (int a = 0; a <= p; ++a) {
            for (int b = 0; b <= (dims > 1 ? p - a : 0); ++b) {
                for (int c = 0; c <= (dims > 2 ? p - a - b : 0); ++c) {
                    add(a, b, c);
                }
            }
        }
    };
    // Tensor products of degree p in each of `dims` variables; with
    // `serendipity`, at most one variable reaches the degree 2.
    const auto tensor = [&](int p, int dims, bool serendipity) {
        for (int a = 0; a <= p; ++a) {
            for (int b = 0; b <= (dims > 1 ? p : 0); ++b) {
                for (int c = 0; c <= (dims > 2 ? p : 0); ++c) {
                    const int squares = (a == 2 ? 1 : 0) + (b == 2 ? 1 : 0) + (c == 2 ? 1 : 0);
                    if (!serendipity || squares <= 1) {
                        add(a, b, c);
                    }
                }
            }
        }
    };
    // Prism: P_p(xi, eta) x {zeta^0 .. zeta^q}, plus P1(xi, eta) x zeta^2
    // for the 15-node serendipity prism.
    const auto prism = [&](int p, int q, bool serendipity) {
        for (int c = 0; c <= q; ++c) {
            for (int a = 0; a <= p; ++a) {
                for (int b = 0; b <= p - a; ++b) {
                    add(a, b, c);
                }
            }
        }
        if (serendipity) {
            add(0, 0, 2);
            add(1, 0, 2);
            add(0, 1, 2);
        }
    };

    switch (type) {
    case ElementType::Line2:
        tensor(1, 1, false);
        break;
    case ElementType::Line3:
        tensor(2, 1, false);
        break;
    case ElementType::Tri3:
        complete(1, 2);
        break;
    case ElementType::Tri6:
        complete(2, 2);
        break;
    case ElementType::Quad4:
        tensor(1, 2, false);
        break;
    case ElementType::Quad8:
        tensor(2, 2, true);
        break;
    case ElementType::Quad9:
        tensor(2, 2, false);
        break;
    case ElementType::Tet4:
        complete(1, 3);
        break;
    case ElementType::Tet10:
        complete(2, 3);
        break;
    case ElementType::Hex8:
        tensor(1, 3, false);
        break;
    case ElementType::Hex20:
        tensor(2, 3, true);
        break;
    case ElementType::Hex27:
        tensor(2, 3, false);
        break;
    case ElementType::Wedge6:
        prism(1, 1, false);
        break;
    case ElementType::Wedge15:
        prism(2, 1, true);
        break;
    case ElementType::Wedge18:
        prism(2, 2, false);
        break;
    default:
        break;
    }
    return space;
}

// Coefficients of the Lagrange basis in the monomials of the space:
// N_i(xi) = sum_j m_j(xi) C(j, i).
struct Basis
{
    int dimension = 0;
    std::vector<Exponents> space;
    Eigen::MatrixXd coefficients;
};

double power(double x, int k)
{
    double result = 1.0;
    for (int i = 0; i < k; ++i) {
        result *= x;
    }
    return result;
}

Basis makeBasis(ElementType type)
{
    Basis basis;
    basis.dimension = elementTypeInfo(type).dimension;
    basis.space = spaceOf(type);
    const auto nodes = referenceNodeCoordinates(type);
    const auto n = static_cast<Eigen::Index>(nodes.size());
    if (static_cast<Eigen::Index>(basis.space.size()) != n) {
        throw std::logic_error("shape functions: the space does not match the nodes");
    }
    Eigen::MatrixXd vandermonde(n, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        const auto& x = nodes[static_cast<std::size_t>(i)];
        for (Eigen::Index j = 0; j < n; ++j) {
            const Exponents& e = basis.space[static_cast<std::size_t>(j)];
            vandermonde(i, j) = power(x[0], e[0]) * power(x[1], e[1]) * power(x[2], e[2]);
        }
    }
    const Eigen::FullPivLU<Eigen::MatrixXd> lu(vandermonde);
    if (!lu.isInvertible()) {
        throw std::logic_error("shape functions: singular Vandermonde matrix");
    }
    basis.coefficients = lu.inverse();
    return basis;
}

// Tables, prepared once per type on first use (thread safe).
const Basis& basisOf(ElementType type)
{
    static const auto tables = [] {
        std::array<Basis, kElementTypeCount> all{};
        for (const ElementType t : allElementTypes()) {
            if (hasShapeFunctions(t) && t != ElementType::Pyramid5) {
                all.at(static_cast<std::size_t>(t)) = makeBasis(t);
            }
        }
        return all;
    }();
    return tables.at(static_cast<std::size_t>(type));
}

// Pyramid5: rational functions (design 006 §5.1). Base nodes 0..3 at
// (-1,-1), (1,-1), (1,1), (-1,1) on z = 0, apex at (0, 0, 1).
void evaluatePyramid(const std::array<double, 3>& xi, ShapeValues& out)
{
    constexpr std::array<std::array<double, 2>, 4> kSigns = {
        {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}}};
    constexpr double kApex = 1e-12;
    const double x = xi[0];
    const double y = xi[1];
    const double z = xi[2];
    const double r = 1.0 - z;
    // xy / r -> 0 at the apex, where |x|, |y| <= r.
    const bool apex = r < kApex;
    const double xyOverR = apex ? 0.0 : x * y / r;
    const double xyOverR2 = apex ? 0.0 : x * y / (r * r);
    const double yOverR = apex ? 0.0 : y / r;
    const double xOverR = apex ? 0.0 : x / r;
    out.count = 5;
    for (std::size_t i = 0; i < 4; ++i) {
        const double sx = kSigns.at(i)[0];
        const double sy = kSigns.at(i)[1];
        out.n.at(i) = 0.25 * (r + sx * x + sy * y + sx * sy * xyOverR);
        out.dn.at(i) = {0.25 * (sx + sx * sy * yOverR), 0.25 * (sy + sx * sy * xOverR),
                        0.25 * (-1.0 + sx * sy * xyOverR2)};
    }
    out.n.at(4) = z;
    out.dn.at(4) = {0.0, 0.0, 1.0};
}

} // namespace

bool hasShapeFunctions(ElementType type) noexcept
{
    switch (type) {
    case ElementType::Line2:
    case ElementType::Line3:
    case ElementType::Tri3:
    case ElementType::Tri6:
    case ElementType::Quad4:
    case ElementType::Quad8:
    case ElementType::Quad9:
    case ElementType::Tet4:
    case ElementType::Tet10:
    case ElementType::Hex8:
    case ElementType::Hex20:
    case ElementType::Hex27:
    case ElementType::Wedge6:
    case ElementType::Wedge15:
    case ElementType::Wedge18:
    case ElementType::Pyramid5:
        return true;
    default:
        return false;
    }
}

void evaluateShape(ElementType type, const std::array<double, 3>& xi, ShapeValues& out)
{
    assert(hasShapeFunctions(type));
    if (type == ElementType::Pyramid5) {
        evaluatePyramid(xi, out);
        return;
    }
    const Basis& basis = basisOf(type);
    const auto count = static_cast<int>(basis.space.size());
    const int dims = basis.dimension;

    // Monomials and their derivatives at xi, in fixed storage.
    std::array<double, kMaxNodes> m{};
    std::array<std::array<double, 3>, kMaxNodes> dm{};
    for (int j = 0; j < count; ++j) {
        const Exponents& e = basis.space[static_cast<std::size_t>(j)];
        std::array<double, 3> p{};
        std::array<double, 3> dp{};
        for (std::size_t k = 0; k < 3; ++k) {
            p.at(k) = power(xi.at(k), e.at(k));
            dp.at(k) = e.at(k) == 0 ? 0.0 : e.at(k) * power(xi.at(k), e.at(k) - 1);
        }
        const auto jj = static_cast<std::size_t>(j);
        m.at(jj) = p[0] * p[1] * p[2];
        dm.at(jj) = {dp[0] * p[1] * p[2], p[0] * dp[1] * p[2], p[0] * p[1] * dp[2]};
    }

    out.count = count;
    for (int i = 0; i < count; ++i) {
        double value = 0.0;
        std::array<double, 3> gradient{};
        for (int j = 0; j < count; ++j) {
            const double c = basis.coefficients(j, i);
            const auto jj = static_cast<std::size_t>(j);
            value += c * m.at(jj);
            for (std::size_t k = 0; k < static_cast<std::size_t>(dims); ++k) {
                gradient.at(k) += c * dm.at(jj).at(k);
            }
        }
        const auto ii = static_cast<std::size_t>(i);
        out.n.at(ii) = value;
        out.dn.at(ii) = gradient;
    }
}

} // namespace fem::core
