#include <fem/core/element_type.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/quadrature.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

using fem::core::ElementNodes;
using fem::core::ElementType;
using Vec3 = Eigen::Vector3d;

namespace {

constexpr double kTolerance = 1e-12;

// The reference nodes of a type moved by an affine map x = A xi + b.
ElementNodes affine(ElementType type, const Eigen::Matrix3d& a, const Vec3& b)
{
    ElementNodes nodes;
    nodes.type = type;
    const auto reference = fem::core::referenceNodeCoordinates(type);
    nodes.count = static_cast<int>(reference.size());
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const Vec3 xi(reference[i][0], reference[i][1], reference[i][2]);
        nodes.x.at(i) = a * xi + b;
    }
    return nodes;
}

fem::core::PointGeometry geometry(const ElementNodes& nodes, const std::array<double, 3>& xi)
{
    const auto g = fem::core::evaluateGeometry(nodes, xi);
    REQUIRE(g.has_value());
    return g.value_or(fem::core::PointGeometry{});
}

// Integral of 1 over the element with a rule of the given degree.
double measureOf(const ElementNodes& nodes, int degree)
{
    double sum = 0.0;
    const auto family = fem::core::elementTypeInfo(nodes.type).family;
    for (const auto& point : fem::core::quadrature(family, degree)) {
        sum += point.weight * geometry(nodes, point.xi).measure;
    }
    return sum;
}

} // namespace

// REQ-006-03: under an affine map the Jacobian is constant, the volume is
// det(A) times the reference volume, and a linear field has exact physical
// derivatives.
TEST_CASE("affine elements map volumes and gradients exactly", "[core][isoparametric]")
{
    const ElementType type =
        GENERATE(ElementType::Tet4, ElementType::Tet10, ElementType::Hex8, ElementType::Hex20,
                 ElementType::Hex27, ElementType::Wedge6, ElementType::Wedge15,
                 ElementType::Wedge18, ElementType::Pyramid5);
    INFO("type " << std::string(fem::core::elementTypeInfo(type).name));
    Eigen::Matrix3d a;
    a << 2.0, 0.3, 0.1, //
        0.2, 1.5, -0.2, //
        0.0, 0.4, 3.0;
    const Vec3 b(1.0, -2.0, 0.5);
    const ElementNodes nodes = affine(type, a, b);
    const ElementNodes reference = affine(type, Eigen::Matrix3d::Identity(), Vec3::Zero());

    CHECK(std::abs(measureOf(nodes, 4) - a.determinant() * measureOf(reference, 4)) <
          kTolerance * 100.0);

    // u = 1 + 2x - y + 3z: grad u = (2, -1, 3) everywhere.
    const Vec3 gradient(2.0, -1.0, 3.0);
    const auto family = fem::core::elementTypeInfo(type).family;
    for (const auto& point : fem::core::quadrature(family, 2)) {
        const auto g = geometry(nodes, point.xi);
        Vec3 computed = Vec3::Zero();
        for (int i = 0; i < g.shape.count; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            const Vec3& x = nodes.x.at(ii);
            const double u = 1.0 + gradient.dot(x);
            computed += u * Vec3(g.shape.dn.at(ii)[0], g.shape.dn.at(ii)[1], g.shape.dn.at(ii)[2]);
        }
        CHECK((computed - gradient).norm() < 1e-10);
    }
}

// REQ-006-03: distorted hexahedron: the volume of a non-affine element.
TEST_CASE("a distorted hexahedron has the right volume", "[core][isoparametric]")
{
    // Unit cube with the top face raised at one corner: x in [0,1]^2,
    // z from 0 to 1 + x y.
    ElementNodes nodes =
        affine(ElementType::Hex8, 0.5 * Eigen::Matrix3d::Identity(), Vec3(0.5, 0.5, 0.5));
    for (std::size_t i = 0; i < 8; ++i) {
        Vec3& x = nodes.x.at(i);
        if (x.z() > 0.5) {
            x.z() = 1.0 + x.x() * x.y();
        }
    }
    // Volume = integral of (1 + x y) over the unit square = 1.25.
    CHECK(std::abs(measureOf(nodes, 5) - 1.25) < kTolerance);
}

// REQ-006-04: an inverted solid is refused.
TEST_CASE("an inverted element is refused", "[core][isoparametric]")
{
    Eigen::Matrix3d mirror = Eigen::Matrix3d::Identity();
    mirror(0, 0) = -1.0;
    const ElementNodes inverted = affine(ElementType::Tet4, mirror, Vec3::Zero());
    const auto g = fem::core::evaluateGeometry(inverted, {0.25, 0.25, 0.25});
    REQUIRE_FALSE(g.has_value());
    CHECK(g.error().message.find("Jacobian") != std::string::npos);

    // A flat tetrahedron (zero volume) too.
    ElementNodes flat = affine(ElementType::Tet4, Eigen::Matrix3d::Identity(), Vec3::Zero());
    flat.x.at(3).z() = 0.0;
    CHECK_FALSE(fem::core::evaluateGeometry(flat, {0.25, 0.25, 0.25}).has_value());
}

// REQ-006-03: lines and surfaces in space: length, area and the gradient
// along them.
TEST_CASE("lines and surfaces in space", "[core][isoparametric]")
{
    // A line from (1, 2, 3) to (4, 6, 3): length 5.
    ElementNodes line;
    line.type = ElementType::Line2;
    line.count = 2;
    line.x.at(0) = Vec3(1, 2, 3);
    line.x.at(1) = Vec3(4, 6, 3);
    CHECK(std::abs(measureOf(line, 1) - 5.0) < kTolerance);
    // u = x along the line: du/ds along the tangent (0.6, 0.8, 0) gives
    // grad u = 0.6 * tangent.
    const auto g = geometry(line, {0.3, 0.0, 0.0});
    Vec3 grad = Vec3::Zero();
    for (std::size_t i = 0; i < 2; ++i) {
        grad +=
            line.x.at(i).x() * Vec3(g.shape.dn.at(i)[0], g.shape.dn.at(i)[1], g.shape.dn.at(i)[2]);
    }
    CHECK((grad - 0.6 * Vec3(0.6, 0.8, 0.0)).norm() < kTolerance);

    // A right triangle with legs 3 and 4 in a tilted plane: area 6.
    const Vec3 u = Vec3(1, 1, 0).normalized();
    const Vec3 v = Vec3(-1, 1, 1).normalized();
    ElementNodes triangle;
    triangle.type = ElementType::Tri3;
    triangle.count = 3;
    triangle.x.at(0) = Vec3(1, 1, 1);
    triangle.x.at(1) = triangle.x.at(0) + 3.0 * u;
    triangle.x.at(2) = triangle.x.at(0) + 4.0 * v;
    CHECK(std::abs(measureOf(triangle, 1) - 6.0) < kTolerance);

    // A planar quadrilateral (trapezoid) in the plane z = 2: area 1.5.
    ElementNodes quad;
    quad.type = ElementType::Quad4;
    quad.count = 4;
    quad.x.at(0) = Vec3(0, 0, 2);
    quad.x.at(1) = Vec3(2, 0, 2);
    quad.x.at(2) = Vec3(1, 1, 2);
    quad.x.at(3) = Vec3(0, 1, 2);
    CHECK(std::abs(measureOf(quad, 3) - 1.5) < kTolerance);

    // Zero length is refused.
    line.x.at(1) = line.x.at(0);
    CHECK_FALSE(fem::core::evaluateGeometry(line, {0.0, 0.0, 0.0}).has_value());
}
