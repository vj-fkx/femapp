#include <fem/core/element_type.hpp>
#include <fem/core/shape_functions.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>

using fem::core::ElementFamily;
using fem::core::ElementType;
using fem::core::ShapeValues;

namespace {

constexpr int kPoints = 50;

// A random point inside the reference element of the family.
std::array<double, 3> randomPoint(ElementFamily family, std::mt19937& random)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> symmetric(-1.0, 1.0);
    switch (family) {
    case ElementFamily::Line:
        return {symmetric(random), 0.0, 0.0};
    case ElementFamily::Triangle: {
        const double u = unit(random);
        const double v = unit(random) * (1.0 - u);
        return {u, v, 0.0};
    }
    case ElementFamily::Quadrilateral:
        return {symmetric(random), symmetric(random), 0.0};
    case ElementFamily::Tetrahedron: {
        const double u = unit(random);
        const double v = unit(random) * (1.0 - u);
        const double w = unit(random) * (1.0 - u - v);
        return {u, v, w};
    }
    case ElementFamily::Hexahedron:
        return {symmetric(random), symmetric(random), symmetric(random)};
    case ElementFamily::Wedge: {
        const double u = unit(random);
        const double v = unit(random) * (1.0 - u);
        return {u, v, symmetric(random)};
    }
    case ElementFamily::Pyramid: {
        const double z = 0.9 * unit(random);
        const double r = 1.0 - z;
        return {r * symmetric(random), r * symmetric(random), z};
    }
    case ElementFamily::Point:
        break;
    }
    return {};
}

const auto kTypes = {
    ElementType::Line2,  ElementType::Line3,   ElementType::Tri3,    ElementType::Tri6,
    ElementType::Quad4,  ElementType::Quad8,   ElementType::Quad9,   ElementType::Tet4,
    ElementType::Tet10,  ElementType::Hex8,    ElementType::Hex20,   ElementType::Hex27,
    ElementType::Wedge6, ElementType::Wedge15, ElementType::Wedge18, ElementType::Pyramid5};

} // namespace

// REQ-006-01, REQ-006-02: Kronecker delta at the nodes, partition of unity
// and derivatives that add up to zero.
TEST_CASE("shape functions are a Lagrange basis on the Gmsh nodes", "[core][shape]")
{
    const ElementType type = GENERATE(values(kTypes));
    const auto& info = fem::core::elementTypeInfo(type);
    INFO("type " << std::string(info.name));
    REQUIRE(fem::core::hasShapeFunctions(type));

    const auto nodes = fem::core::referenceNodeCoordinates(type);
    ShapeValues values;
    for (std::size_t j = 0; j < nodes.size(); ++j) {
        fem::core::evaluateShape(type, nodes[j], values);
        REQUIRE(values.count == info.nodeCount);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const double expected = i == j ? 1.0 : 0.0;
            INFO("node " << j << ", function " << i);
            CHECK(std::abs(values.n.at(i) - expected) < 1e-12);
        }
    }

    std::mt19937 random(7);
    for (int p = 0; p < kPoints; ++p) {
        const auto xi = randomPoint(info.family, random);
        fem::core::evaluateShape(type, xi, values);
        double sum = 0.0;
        std::array<double, 3> gradient{};
        for (int i = 0; i < values.count; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            sum += values.n.at(ii);
            for (std::size_t k = 0; k < 3; ++k) {
                gradient.at(k) += values.dn.at(ii).at(k);
            }
        }
        CHECK(std::abs(sum - 1.0) < 1e-12);
        for (const double g : gradient) {
            CHECK(std::abs(g) < 1e-11);
        }
    }
}

// REQ-006-01: derivatives agree with centred finite differences.
TEST_CASE("shape function derivatives match finite differences", "[core][shape]")
{
    const ElementType type = GENERATE(values(kTypes));
    const auto& info = fem::core::elementTypeInfo(type);
    INFO("type " << std::string(info.name));
    constexpr double kStep = 1e-6;
    constexpr double kTolerance = 1e-6;

    std::mt19937 random(11);
    ShapeValues at;
    ShapeValues plus;
    ShapeValues minus;
    for (int p = 0; p < kPoints; ++p) {
        const auto xi = randomPoint(info.family, random);
        fem::core::evaluateShape(type, xi, at);
        for (int k = 0; k < info.dimension; ++k) {
            const auto kk = static_cast<std::size_t>(k);
            auto forward = xi;
            auto backward = xi;
            forward.at(kk) += kStep;
            backward.at(kk) -= kStep;
            fem::core::evaluateShape(type, forward, plus);
            fem::core::evaluateShape(type, backward, minus);
            for (int i = 0; i < at.count; ++i) {
                const auto ii = static_cast<std::size_t>(i);
                const double fd = (plus.n.at(ii) - minus.n.at(ii)) / (2.0 * kStep);
                INFO("function " << i << ", direction " << k);
                CHECK(std::abs(fd - at.dn.at(ii).at(kk)) < kTolerance * (1.0 + std::abs(fd)));
            }
        }
    }
}

// REQ-006-01: types outside the list have no shape functions.
TEST_CASE("higher order types have no shape functions", "[core][shape]")
{
    CHECK_FALSE(fem::core::hasShapeFunctions(ElementType::Tri10));
    CHECK_FALSE(fem::core::hasShapeFunctions(ElementType::Pyramid13));
    CHECK_FALSE(fem::core::hasShapeFunctions(ElementType::Point1));
}
