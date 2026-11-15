// REQ-006-05: evaluating shape functions and element geometry does not
// allocate. A test executable of its own, because it replaces the global
// operator new to count the allocations.

#include <fem/core/element_type.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/quadrature.hpp>
#include <fem/core/shape_functions.hpp>

#include <catch2/catch_test_macros.hpp>

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
