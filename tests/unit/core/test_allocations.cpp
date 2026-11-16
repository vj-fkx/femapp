// REQ-006-05: evaluating shape functions and element geometry does not
// allocate; REQ-006-12: nor do the truss element of 007 and the beam element
// of 008. A test executable of its own, because it replaces the global
// operator new to count the allocations.

#include <fem/core/beam_element.hpp>
#include <fem/core/element_type.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/quadrature.hpp>
#include <fem/core/shape_functions.hpp>
#include <fem/core/truss_element.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool> gCounting{false};
std::atomic<long> gAllocations{0};

} // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(misc-new-delete-overloads)
void* operator new(std::size_t size)
{
    if (gCounting) {
        ++gAllocations;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t /*size*/) noexcept
{
    std::free(p);
}
// NOLINTEND(misc-new-delete-overloads)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-no-malloc)

TEST_CASE("shape functions and geometry do not allocate", "[core][allocations]")
{
    const auto types = {fem::core::ElementType::Line3,   fem::core::ElementType::Tri6,
                        fem::core::ElementType::Quad8,   fem::core::ElementType::Tet10,
                        fem::core::ElementType::Hex27,   fem::core::ElementType::Wedge15,
                        fem::core::ElementType::Pyramid5};
    fem::core::ShapeValues values;
    fem::core::ElementNodes nodes;
    // First calls prepare the tables (allowed to allocate).
    for (const auto type : types) {
        fem::core::evaluateShape(type, {0.1, 0.1, 0.1}, values);
        (void)fem::core::quadrature(fem::core::elementTypeInfo(type).family, 2);
    }

    double sink = 0.0;
    gCounting = true;
    for (const auto type : types) {
        const auto reference = fem::core::referenceNodeCoordinates(type);
        nodes.type = type;
        nodes.count = static_cast<int>(reference.size());
        for (std::size_t i = 0; i < reference.size(); ++i) {
            nodes.x.at(i) = Eigen::Vector3d(reference[i][0], reference[i][1], reference[i][2]) +
                            Eigen::Vector3d(0.0, 0.0, 0.01 * static_cast<double>(i));
        }
        const auto family = fem::core::elementTypeInfo(type).family;
        for (const auto& point : fem::core::quadrature(family, 2)) {
            fem::core::evaluateShape(type, point.xi, values);
            sink += values.n[0];
            const auto g = fem::core::evaluateGeometry(nodes, point.xi);
            sink += g ? g->measure : 0.0;
        }
    }
    gCounting = false;
    CHECK(gAllocations == 0);
    CHECK(sink != 0.0);
}

TEST_CASE("the truss element does not allocate", "[core][allocations]")
{
    // On the stack: a heap vector here trips GCC's mismatched-new-delete
    // check against the counting operator new above.
    const std::array<fem::core::BarData, 1> bars{};
    const fem::core::TrussElement element(fem::core::ElementKind::Truss3D, bars);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = Eigen::Vector3d(0.0, 0.0, 0.0);
    nodes.x[1] = Eigen::Vector3d(1.0, 2.0, 3.0);
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(6, 6);
    Eigen::VectorXd vector = Eigen::VectorXd::Zero(6);

    gAllocations = 0;
    gCounting = true;
    const bool computed = element.compute(nodes, 0, matrix, vector).has_value();
    gCounting = false;
    CHECK(computed);
    CHECK(gAllocations == 0);
}

TEST_CASE("the beam element does not allocate", "[core][allocations]")
{
    // With connections (internal dofs), offsets, a shear centre and a
    // foundation: every branch of compute().
    std::array<fem::core::BeamData, 1> beams{};
    fem::core::BeamData& beam = beams[0];
    beam.axial = 1e6;
    beam.torsional = 1e3;
    beam.bendingY = beam.bendingZ = 1e4;
    beam.shearY = beam.shearZ = 1e5;
    beam.shearCentre = Eigen::Vector2d(0.01, 0.02);
    beam.offsets[0] = Eigen::Vector3d(0.1, 0.0, 0.0);
    beam.springs[1][5] = 0.0;
    beam.springs[1][4] = 1e4;
    beam.foundation = Eigen::Vector4d(1.0, 2.0, 3.0, 4.0);
    const fem::core::BeamElement element(fem::core::ElementKind::Beam3D, beams);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = Eigen::Vector3d(0.0, 0.0, 0.0);
    nodes.x[1] = Eigen::Vector3d(3.0, 0.0, 0.0);
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(12, 12);
    Eigen::VectorXd vector = Eigen::VectorXd::Zero(12);

    gAllocations = 0;
    gCounting = true;
    const bool computed = element.compute(nodes, 0, matrix, vector).has_value();
    gCounting = false;
    CHECK(computed);
    CHECK(gAllocations == 0);
}
