#include <fem/core/element_type.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/mesh_measure.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Eigen/Geometry>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinRel;
using fem::core::allElementTypes;
using fem::core::ElementFamily;
using fem::core::ElementType;
using fem::core::elementTypeInfo;
using fem::core::ExternalId;
using fem::core::Mesh;
using fem::core::MeshBuilder;

namespace {

// Measure of Gmsh's reference element of each family.
double referenceMeasure(ElementFamily family)
{
    switch (family) {
    case ElementFamily::Point:
        return 1.0;
    case ElementFamily::Line:
        return 2.0; // [-1, 1]
    case ElementFamily::Triangle:
        return 0.5;
    case ElementFamily::Quadrilateral:
        return 4.0; // [-1, 1]^2
    case ElementFamily::Tetrahedron:
        return 1.0 / 6.0;
    case ElementFamily::Hexahedron:
        return 8.0; // [-1, 1]^3
    case ElementFamily::Wedge:
        return 1.0; // triangle 1/2 times height 2
    case ElementFamily::Pyramid:
        return 4.0 / 3.0; // base 4, height 1
    }
    return 0.0;
}

// One element of `type` at the given node positions.
Mesh singleElement(ElementType type, const std::vector<Eigen::Vector3d>& positions)
{
    MeshBuilder builder;
    std::vector<ExternalId> ids;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto id = static_cast<ExternalId>(i + 1);
        REQUIRE(builder.addNode(id, positions[i]).has_value());
        ids.push_back(id);
    }
    REQUIRE(builder.addElement(type, 1, ids).has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

std::vector<Eigen::Vector3d> referencePositions(ElementType type)
{
    std::vector<Eigen::Vector3d> positions;
    for (const auto& p : fem::core::referenceNodeCoordinates(type)) {
        positions.emplace_back(p[0], p[1], p[2]);
    }
    return positions;
}

} // namespace

// REQ-003-03, REQ-003-08: the documented node order gives positive measures.
TEST_CASE("every reference element has its known positive measure", "[core][measure]")
{
    for (const ElementType type : allElementTypes()) {
        const auto& info = elementTypeInfo(type);
        INFO(info.name);
        const Mesh mesh = singleElement(type, referencePositions(type));
        const double measure = fem::core::elementMeasure(mesh, 0);
        CHECK_THAT(measure, WithinRel(referenceMeasure(info.family), 1e-12));
    }
}

// REQ-003-08: the measure follows rigid motions and scaling.
TEST_CASE("measure scales with the element", "[core][measure]")
{
    const Eigen::Matrix3d rotation =
        Eigen::AngleAxisd(0.7, Eigen::Vector3d(1, 2, 3).normalized()).toRotationMatrix();
    constexpr double kScale = 2.5;
    const Eigen::Vector3d shift(10, -4, 7);

    for (const ElementType type : {ElementType::Line3, ElementType::Tet10, ElementType::Hex27,
                                   ElementType::Wedge15, ElementType::Pyramid13}) {
        const auto& info = elementTypeInfo(type);
        INFO(info.name);
        std::vector<Eigen::Vector3d> positions = referencePositions(type);
        for (Eigen::Vector3d& p : positions) {
            p = rotation * (kScale * p) + shift;
        }
        const Mesh mesh = singleElement(type, positions);
        double factor = 1.0;
        for (int d = 0; d < info.dimension; ++d) {
            factor *= kScale;
        }
        CHECK_THAT(fem::core::elementMeasure(mesh, 0),
                   WithinRel(factor * referenceMeasure(info.family), 1e-12));
    }
}

// REQ-003-08: inverted elements have a negative measure.
TEST_CASE("inverted elements have a negative measure", "[core][measure]")
{
    SECTION("tetrahedron")
    {
        std::vector<Eigen::Vector3d> positions = referencePositions(ElementType::Tet4);
        std::swap(positions[1], positions[2]);
        const Mesh mesh = singleElement(ElementType::Tet4, positions);
        CHECK(fem::core::elementMeasure(mesh, 0) < 0.0);
    }
    SECTION("hexahedron")
    {
        std::vector<Eigen::Vector3d> positions = referencePositions(ElementType::Hex8);
        for (Eigen::Vector3d& p : positions) {
            p.z() = -p.z(); // mirror: swaps bottom and top faces
        }
        const Mesh mesh = singleElement(ElementType::Hex8, positions);
        CHECK_THAT(fem::core::elementMeasure(mesh, 0), WithinRel(-8.0, 1e-12));
    }
    SECTION("triangle in the XY plane")
    {
        std::vector<Eigen::Vector3d> positions = referencePositions(ElementType::Tri3);
        std::swap(positions[1], positions[2]);
        const Mesh mesh = singleElement(ElementType::Tri3, positions);
        REQUIRE(mesh.liesInXYPlane());
        CHECK_THAT(fem::core::elementMeasure(mesh, 0), WithinRel(-0.5, 1e-12));
    }
}

// REQ-003-08: a surface element outside the XY plane has no orientation.
TEST_CASE("areas outside the XY plane are positive", "[core][measure]")
{
    std::vector<Eigen::Vector3d> positions = referencePositions(ElementType::Quad4);
    for (Eigen::Vector3d& p : positions) {
        p = Eigen::Vector3d(p.x(), 0.0, p.y()); // in the XZ plane
    }
    std::swap(positions[1], positions[3]); // clockwise when seen from +Y
    const Mesh mesh = singleElement(ElementType::Quad4, positions);
    CHECK_FALSE(mesh.liesInXYPlane());
    CHECK_THAT(fem::core::elementMeasure(mesh, 0), WithinRel(4.0, 1e-12));
}
