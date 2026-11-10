#include "axis_gizmo_mesh.hpp"
#include "axis_labels.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Matchers::WithinAbs;
using fem::render::GizmoVertex;
using fem::render::kGizmoVerticesPerArrow;

// REQ-002-24
TEST_CASE("axis indicator has one arrow per axis with the axis colours", "[render][gizmo]")
{
    const std::vector<GizmoVertex> mesh = fem::render::buildAxisGizmoMesh();
    REQUIRE(mesh.size() == static_cast<std::size_t>(kGizmoVerticesPerArrow) * 3);

    const std::array<std::array<float, 3>, 3> colors = {
        fem::render::kAxisColorX, fem::render::kAxisColorY, fem::render::kAxisColorZ};

    for (std::size_t axis = 0; axis < 3; ++axis) {
        float farthest = 0.0F;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kGizmoVerticesPerArrow); ++i) {
            const GizmoVertex& vertex =
                mesh.at(axis * static_cast<std::size_t>(kGizmoVerticesPerArrow) + i);

            CHECK(vertex.color == colors.at(axis));

            const double normalLength =
                std::hypot(vertex.normal.at(0), vertex.normal.at(1), vertex.normal.at(2));
            CHECK_THAT(normalLength, WithinAbs(1.0, 1e-5));

            // The arrow runs from the origin to 1 along its axis and stays thin.
            const float along = vertex.position.at(axis);
            CHECK(along >= 0.0F);
            CHECK(along <= 1.0F + 1e-6F);
            const double radius =
                std::hypot(vertex.position.at((axis + 1) % 3), vertex.position.at((axis + 2) % 3));
            CHECK(radius < 0.15);
            farthest = std::max(farthest, along);
        }
        CHECK_THAT(farthest, WithinAbs(1.0, 1e-6));
    }
}

// REQ-002-24
TEST_CASE("axis labels sit beyond the arrow tips and map one glyph each", "[render][gizmo]")
{
    const std::vector<fem::render::LabelVertex> quads = fem::render::buildAxisLabelQuads();
    REQUIRE(quads.size() == 18);

    for (std::size_t glyph = 0; glyph < 3; ++glyph) {
        for (std::size_t i = 0; i < 6; ++i) {
            const fem::render::LabelVertex& vertex = quads.at(glyph * 6 + i);

            CHECK(vertex.anchor.at(glyph) > 1.0F);
            CHECK(vertex.anchor.at((glyph + 1) % 3) == 0.0F);
            CHECK(vertex.anchor.at((glyph + 2) % 3) == 0.0F);

            // Each quad samples its own third of the atlas.
            const float u = vertex.texCoord.at(0) * 3.0F;
            CHECK(u >= static_cast<float>(glyph) - 1e-5F);
            CHECK(u <= static_cast<float>(glyph) + 1.0F + 1e-5F);
            // The top of the quad maps to the first row of the image.
            CHECK(vertex.texCoord.at(1) == (vertex.corner.at(1) > 0.0F ? 0.0F : 1.0F));
        }
    }
}
