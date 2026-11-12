#include <fem/core/mesh_surface.hpp>
#include <fem/io/gmsh_reader.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Eigen/Geometry>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

using Catch::Matchers::WithinRel;

// REQ-004-09, REQ-004-10, REQ-004-30: the outer surface of the 1 x 2 x 3
// boxes from feature 003, in tetrahedra, wedges and Hex20. Lives with the io
// tests because it reads the sample files.
TEST_CASE("outer surface of sample boxes is closed and outward", "[io][surface]")
{
    const std::string file = GENERATE("box_tet4.msh", "box_wedge6.msh", "box_hex20.msh");
    INFO(file);
    const auto result = fem::io::readGmsh(std::filesystem::path(FEMAPP_TEST_DATA_DIR) / "gmsh" / file);
    REQUIRE(result.has_value());
    const fem::core::Mesh& mesh = result->mesh;
    const fem::core::MeshSurface surface = fem::core::extractSurface(mesh);
    REQUIRE_FALSE(surface.faces3d.empty());

    double area = 0.0;
    double volume = 0.0;
    std::map<std::pair<int, int>, int> uses;
    for (const fem::core::SurfaceFace& face : surface.faces3d) {
        const auto p = [&](std::size_t i) { return mesh.nodeCoordinates(face.nodes.at(i)); };
        const auto addTriangle = [&](const Eigen::Vector3d& a, const Eigen::Vector3d& b,
                                     const Eigen::Vector3d& c) {
            area += 0.5 * (b - a).cross(c - a).norm();
            volume += a.dot(b.cross(c)) / 6.0;
        };
        addTriangle(p(0), p(1), p(2));
        if (face.nodeCount == 4) {
            addTriangle(p(0), p(2), p(3));
        }
        for (std::size_t i = 0; i < face.nodeCount; ++i) {
            const int a = face.nodes.at(i);
            const int b = face.nodes.at((i + 1) % face.nodeCount);
            ++uses[{std::min(a, b), std::max(a, b)}];
        }
    }

    CHECK_THAT(area, WithinRel(22.0, 1e-9));   // 2 * (1*2 + 1*3 + 2*3)
    CHECK_THAT(volume, WithinRel(6.0, 1e-9));  // positive: faces point out
    CHECK(std::ranges::all_of(uses, [](const auto& entry) { return entry.second == 2; }));
    CHECK(uses.size() == surface.edges3d.size());
}
