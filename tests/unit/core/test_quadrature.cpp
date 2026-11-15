#include <fem/core/element_type.hpp>
#include <fem/core/quadrature.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

using fem::core::ElementFamily;

namespace {

double factorial(int n)
{
    double result = 1.0;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// Integral of u^a over [-1, 1].
double interval(int a)
{
    return a % 2 == 0 ? 2.0 / (a + 1) : 0.0;
}

// Exact integral of x^a y^b z^c over the reference element of the family.
double exact(ElementFamily family, int a, int b, int c)
{
    switch (family) {
    case ElementFamily::Line:
        return interval(a);
    case ElementFamily::Quadrilateral:
        return interval(a) * interval(b);
    case ElementFamily::Hexahedron:
        return interval(a) * interval(b) * interval(c);
    case ElementFamily::Triangle:
        return factorial(a) * factorial(b) / factorial(a + b + 2);
    case ElementFamily::Tetrahedron:
        return factorial(a) * factorial(b) * factorial(c) / factorial(a + b + c + 3);
    case ElementFamily::Wedge:
        return factorial(a) * factorial(b) / factorial(a + b + 2) * interval(c);
    case ElementFamily::Pyramid:
        // x = u (1 - z), y = v (1 - z): I(a) I(b) Beta(c + 1, a + b + 3).
        return interval(a) * interval(b) * factorial(c) * factorial(a + b + 2) /
               factorial(a + b + c + 3);
    case ElementFamily::Point:
        break;
    }
    return 0.0;
}

int dimensionOf(ElementFamily family)
{
    switch (family) {
    case ElementFamily::Line:
        return 1;
    case ElementFamily::Triangle:
    case ElementFamily::Quadrilateral:
        return 2;
    default:
        return 3;
    }
}

bool isSimplex(ElementFamily family)
{
    return family == ElementFamily::Triangle || family == ElementFamily::Tetrahedron;
}

} // namespace

// REQ-006-06, REQ-006-07: every rule integrates the monomials up to its
// degree exactly; positive weights on simplices; the weights add up to the
// measure of the reference element.
TEST_CASE("quadrature rules are exact to their degree", "[core][quadrature]")
{
    const ElementFamily family =
        GENERATE(ElementFamily::Line, ElementFamily::Triangle, ElementFamily::Quadrilateral,
                 ElementFamily::Tetrahedron, ElementFamily::Hexahedron, ElementFamily::Wedge,
                 ElementFamily::Pyramid);
    const int dims = dimensionOf(family);
    const int maxDegree = fem::core::maxQuadratureDegree(family);
    CHECK(maxDegree >= (isSimplex(family) || family == ElementFamily::Wedge ? 4 : 5));

    for (int degree = 0; degree <= maxDegree; ++degree) {
        const auto rule = fem::core::quadrature(family, degree);
        REQUIRE_FALSE(rule.empty());
        if (isSimplex(family)) {
            for (const auto& point : rule) {
                CHECK(point.weight > 0.0);
            }
        }
        for (int a = 0; a <= degree; ++a) {
            for (int b = 0; b <= (dims > 1 ? degree - a : 0); ++b) {
                for (int c = 0; c <= (dims > 2 ? degree - a - b : 0); ++c) {
                    double sum = 0.0;
                    for (const auto& point : rule) {
                        sum += point.weight * std::pow(point.xi[0], a) * std::pow(point.xi[1], b) *
                               std::pow(point.xi[2], c);
                    }
                    const double expected = exact(family, a, b, c);
                    INFO("family " << static_cast<int>(family) << ", degree " << degree
                                   << ", monomial " << a << b << c);
                    CHECK(std::abs(sum - expected) <= 1e-12 * std::max(1.0, std::abs(expected)));
                }
            }
        }
    }
}

TEST_CASE("a point has a single unit weight", "[core][quadrature]")
{
    const auto rule = fem::core::quadrature(ElementFamily::Point, 0);
    REQUIRE(rule.size() == 1);
    CHECK(rule[0].weight == 1.0);
}
