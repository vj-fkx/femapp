#include "line_mesh_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Feature 009, T12 (REQ-009-11 to 14): the input and the words of the MESH
// command (source texts, without a translator).

namespace {

using fem::app::MeshSize;
using fem::geom::EntityId;
using fem::geom::Vec3;

std::string text(const QString& s)
{
    return s.toStdString();
}

} // namespace

TEST_CASE("the MESH command and its shortcut", "[app][mesh]")
{
    CHECK(fem::app::isMeshCommand(QStringLiteral("MESH")));
    CHECK(fem::app::isMeshCommand(QStringLiteral("mesh")));
    CHECK(fem::app::isMeshCommand(QStringLiteral(" Me ")));
    CHECK_FALSE(fem::app::isMeshCommand(QStringLiteral("M")));
    CHECK_FALSE(fem::app::isMeshCommand(QStringLiteral("MESHES")));
    CHECK(fem::app::completeMeshCommand(QStringLiteral("me")) == QStringLiteral("MESH"));
    CHECK(fem::app::completeMeshCommand(QStringLiteral("MES")) == QStringLiteral("MESH"));
    CHECK_FALSE(fem::app::completeMeshCommand(QStringLiteral("")).has_value());
    CHECK_FALSE(fem::app::completeMeshCommand(QStringLiteral("MO")).has_value());
}

TEST_CASE("the size typed at the MESH prompt", "[app][mesh]")
{
    CHECK(fem::app::parseMeshSize(QString()).kind == MeshSize::Kind::Default);
    CHECK(fem::app::parseMeshSize(QStringLiteral("  ")).kind == MeshSize::Kind::Default);
    const MeshSize half = fem::app::parseMeshSize(QStringLiteral("0.5"));
    CHECK(half.kind == MeshSize::Kind::Length);
    CHECK(half.length == 0.5);
    CHECK(fem::app::parseMeshSize(QStringLiteral("2")).length == 2.0);
    for (const char* invalid : {"abc", "-1", "0", "1,2", "@1,1", "0,5"}) {
        INFO(invalid);
        CHECK(fem::app::parseMeshSize(QString::fromUtf8(invalid)).kind == MeshSize::Kind::Invalid);
    }
}

TEST_CASE("the report of a mesh", "[app][mesh]")
{
    fem::mesher::LineMeshReport report;
    report.nodes = 5;
    report.elements = 4;
    report.mergedNodes = 2;
    for (int i = 0; i < 23; ++i) {
        report.splits.push_back({.kind = fem::mesher::SplitKind::Crossing,
                                 .position = Vec3(2.0, 2.5, 0.0),
                                 .entities = {EntityId{3}, EntityId{7}}});
    }
    report.splits.front().kind = fem::mesher::SplitKind::TJoint;
    const QStringList lines = fem::app::meshReportLines(report, 20);
    // Counts, details, 20 splits and the rest.
    REQUIRE(lines.size() == 2 + 20 + 1);
    CHECK(text(lines[0]) == "Mesh: 5 nodes, 4 elements.");
    CHECK(text(lines[1]).find("23 split") != std::string::npos);
    CHECK(text(lines[1]).find("2 node") != std::string::npos);
    CHECK(text(lines[1]).find("0 piece") != std::string::npos);
    CHECK(text(lines[2]) == "  T joint at (2, 2.5, 0): entities 3, 7");
    CHECK(text(lines[3]) == "  Crossing at (2, 2.5, 0): entities 3, 7");
    CHECK(text(lines.back()).find("3 more") != std::string::npos);
    // Without splits: only the counts.
    CHECK(fem::app::meshReportLines(
              {.nodes = 2, .elements = 1, .splits = {}, .mergedNodes = 0, .ignored = {}})
              .size() == 2);
}

TEST_CASE("why a drawing could not be meshed", "[app][mesh]")
{
    const QStringList none = fem::app::meshErrorLines(
        {.kind = fem::mesher::LineMeshError::Kind::NoLines, .overlaps = {}, .items = {}});
    REQUIRE(none.size() == 1);
    CHECK(text(none[0]) == "Could not mesh the drawing: the drawing has no lines.");

    fem::mesher::LineMeshError overlaps{
        .kind = fem::mesher::LineMeshError::Kind::Overlaps, .overlaps = {}, .items = {}};
    overlaps.overlaps.push_back({.first = EntityId{12},
                                 .second = EntityId{15},
                                 .from = Vec3(2, 0, 0),
                                 .to = Vec3(3, 0, 0)});
    const QStringList lines = fem::app::meshErrorLines(overlaps);
    REQUIRE(lines.size() == 2);
    CHECK(text(lines[0]).find("1 pair") != std::string::npos);
    CHECK(text(lines[1]) == "  Entities 12 and 15 overlap from (2, 0, 0) to (3, 0, 0).");
}
