#include <fem/core/mesh_builder.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

// Feature 010, T08 (design §5.3, §5.4): REQ-010-07, 11, 12, 14, 15, 20.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::EntityId;
using fem::geom::Vec3;
using s::IssueKind;

EntityId addLine(Drawing& drawing, const Vec3& a, const Vec3& b)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = fem::geom::LineEntity{.start = a, .end = b}});
    drawing.apply(edit);
    return id;
}

EntityId addPoint(Drawing& drawing, const Vec3& p)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = fem::geom::PointEntity{.position = p}});
    drawing.apply(edit);
    return id;
}

s::ItemId added(s::Document& document, s::EditResult result)
{
    REQUIRE(result.has_value());
    document.apply(*result);
    return s::idOf(result->added.back());
}

// The 2 x 2 frame of 009 T10: columns, beams, points at the three bases and
// at the top corner.
struct Frame
{
    Drawing drawing;
    std::vector<EntityId> bars;
    std::vector<EntityId> bases;
    EntityId top{};
};

void drawFrame(Frame& f)
{
    for (int col = 0; col < 3; ++col) {
        for (int st = 0; st < 2; ++st) {
            f.bars.push_back(addLine(f.drawing, Vec3(4.0 * col, 3.0 * st, 0.0),
                                     Vec3(4.0 * col, 3.0 * (st + 1), 0.0)));
        }
    }
    for (int st = 1; st <= 2; ++st) {
        for (int b = 0; b < 2; ++b) {
            f.bars.push_back(addLine(f.drawing, Vec3(4.0 * b, 3.0 * st, 0.0),
                                     Vec3(4.0 * (b + 1), 3.0 * st, 0.0)));
        }
    }
    for (int col = 0; col < 3; ++col) {
        f.bases.push_back(addPoint(f.drawing, Vec3(4.0 * col, 0.0, 0.0)));
    }
    f.top = addPoint(f.drawing, Vec3(0.0, 6.0, 0.0));
}

s::MeshContext meshOf(const Drawing& drawing)
{
    auto meshed = fem::mesher::meshLines(drawing);
    REQUIRE(meshed.has_value());
    s::MeshContext context;
    context.mesh = std::make_shared<const c::Mesh>(std::move(meshed->mesh));
    context.map = std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map));
    context.generation = 1;
    context.drawingRevision = drawing.revision();
    return context;
}

s::Targets entities(const std::vector<EntityId>& ids)
{
    s::Targets t;
    t.entities = ids;
    return t;
}

// The document of the frame: steel, a rectangle, beams on every bar, the
// bases fixed and a load at the top corner.
struct Setup
{
    s::Document document;
    s::ItemId loadCase{};
};

void prepare(Setup& setup, const Frame& f)
{
    s::Document& d = setup.document;
    added(d, s::addItem(
                 d, s::MaterialItem{
                        .id = {},
                        .data = {.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3}}));
    added(d, s::addItem(d, s::SectionItem{.id = {},
                                          .data = {.name = "bar",
                                                   .shape = c::Rectangle{.b = 0.3, .h = 0.5}}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = entities(f.bars),
                                           .data = {.kind = c::ElementKind::Beam2D,
                                                    .material = "steel",
                                                    .section = "bar"}}));
    s::SupportItem fixed{.id = {}, .targets = entities(f.bases), .held = {}};
    fixed.held[static_cast<std::size_t>(c::DofType::U)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::V)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::RZ)] = true;
    added(d, s::assign(d, fixed));
    setup.loadCase = added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "lateral"}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = setup.loadCase,
                                       .targets = entities({f.top}),
                                       .action = c::NodalForce{.force = Vec3(10.0, -20.0, 0.0)}}));
}

// The same frame built by hand (009 T10): nodes numbered backwards from
// 109, targets by ids.
c::Model handFrame()
{
    c::MeshBuilder builder;
    std::vector<Vec3> positions;
    for (int col = 0; col < 3; ++col) {
        for (int st = 0; st <= 2; ++st) {
            positions.emplace_back(4.0 * col, 3.0 * st, 0.0);
        }
    }
    const auto idOf = [&](const Vec3& p) {
        const auto it = std::ranges::find(positions, p);
        return static_cast<c::ExternalId>(109 - (it - positions.begin()));
    };
    for (const Vec3& p : positions) {
        REQUIRE(builder.addNode(idOf(p), p).has_value());
    }
    std::vector<c::ElementHandle> bars;
    c::ExternalId next = 500;
    const auto bar = [&](const Vec3& a, const Vec3& b) {
        const std::array<c::ExternalId, 2> nodes{idOf(a), idOf(b)};
        const auto handle = builder.addElement(c::ElementType::Line2, next++, nodes);
        REQUIRE(handle.has_value());
        bars.push_back(*handle);
    };
    for (int col = 0; col < 3; ++col) {
        for (int st = 0; st < 2; ++st) {
            bar(Vec3(4.0 * col, 3.0 * st, 0.0), Vec3(4.0 * col, 3.0 * (st + 1), 0.0));
        }
    }
    for (int st = 1; st <= 2; ++st) {
        for (int b = 0; b < 2; ++b) {
            bar(Vec3(4.0 * b, 3.0 * st, 0.0), Vec3(4.0 * (b + 1), 3.0 * st, 0.0));
        }
    }
    REQUIRE(builder.addGroup("bars", 1, std::move(bars)).has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    c::Model model;
    model.mesh = std::make_shared<const c::Mesh>(std::move(*mesh));
    model.materials.push_back({.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3});
    model.sections.push_back({.name = "bar", .shape = c::Rectangle{.b = 0.3, .h = 0.5}});
    model.assignments.push_back({.elements = c::GroupRef{"bars"},
                                 .kind = c::ElementKind::Beam2D,
                                 .material = "steel",
                                 .section = "bar"});
    for (const auto dof : {c::DofType::U, c::DofType::V, c::DofType::RZ}) {
        model.restraints.push_back(
            {.nodes = c::NodeIds{{idOf(Vec3(0, 0, 0)), idOf(Vec3(4, 0, 0)), idOf(Vec3(8, 0, 0))}},
             .dof = dof,
             .value = 0.0});
    }
    model.nodalForces.push_back(
        {.nodes = c::NodeIds{{idOf(Vec3(0, 6, 0))}}, .force = Vec3(10.0, -20.0, 0.0)});
    return model;
}

// The same displacements and rotations at the same places (1e-12).
void sameAnalysis(const c::Model& a, const c::Model& b)
{
    const auto ra = c::runLinearStatic(a);
    const auto rb = c::runLinearStatic(b);
    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());
    double largest = 0.0;
    for (const Vec3& d : rb->displacements) {
        largest = std::max(largest, d.norm());
    }
    REQUIRE(largest > 0.0);
    REQUIRE(a.mesh->nodeCount() == b.mesh->nodeCount());
    for (c::NodeIndex n = 0; n < a.mesh->nodeCount(); ++n) {
        const Vec3 p = a.mesh->nodeCoordinates(n);
        c::NodeIndex m = 0;
        while (m < b.mesh->nodeCount() && (b.mesh->nodeCoordinates(m) - p).norm() > 1e-12) {
            ++m;
        }
        REQUIRE(m < b.mesh->nodeCount());
        const auto i = static_cast<std::size_t>(n);
        const auto j = static_cast<std::size_t>(m);
        CHECK((ra->displacements[i] - rb->displacements[j]).norm() <= 1e-12 * largest);
        CHECK(std::abs(ra->rotations[i].z() - rb->rotations[j].z()) <=
              1e-12 * std::max(1.0, std::abs(rb->rotations[j].z())));
    }
}

bool has(const std::vector<s::BuildIssue>& issues, IssueKind kind,
         std::optional<s::ItemId> item = std::nullopt)
{
    return std::ranges::any_of(issues, [&](const s::BuildIssue& i) {
        return i.kind == kind && (!item || i.item == item);
    });
}

} // namespace

// AC-3: REQ-010-07, 11, 12, 15, 20.
TEST_CASE("the frame prepared in the document analyses like the frame by hand", "[setup][build]")
{
    Frame f;
    drawFrame(f);
    Setup setup;
    prepare(setup, f);
    const s::BuildResult built =
        s::buildModel(setup.document, f.drawing, meshOf(f.drawing), {.item = setup.loadCase});
    for (const s::BuildIssue& issue : built.issues) {
        INFO(static_cast<int>(issue.kind) << " " << issue.detail);
        CHECK(issue.severity != s::Severity::Error);
    }
    CHECK(c::validate(built.model).empty());
    sameAnalysis(built.model, handFrame());
}

// REQ-010-14, 20: what goes wrong is listed, with the item.
TEST_CASE("the problems of a build", "[setup][build]")
{
    Frame f;
    drawFrame(f);
    Setup setup;
    prepare(setup, f);
    const s::LoadSelection load{.item = setup.loadCase};

    SECTION("no mesh")
    {
        const auto built = s::buildModel(setup.document, f.drawing, {}, load);
        CHECK(has(built.issues, IssueKind::NoMesh));
    }
    SECTION("the drawing changed after the mesh")
    {
        const s::MeshContext mesh = meshOf(f.drawing);
        addLine(f.drawing, Vec3(20, 0, 0), Vec3(21, 0, 0));
        CHECK(has(s::buildModel(setup.document, f.drawing, mesh, load).issues,
                  IssueKind::MeshOutOfDate));
    }
    SECTION("an entity that is gone, and one that was never meshed")
    {
        s::Document& d = setup.document;
        const s::ItemId gone =
            added(d, s::assign(d, s::OffsetItem{
                                      .id = {}, .targets = entities({EntityId{999}}), .data = {}}));
        const s::MeshContext mesh = meshOf(f.drawing);
        CHECK(has(s::buildModel(d, f.drawing, mesh, load).issues, IssueKind::MissingEntity, gone));
        // A mesh without a map (imported): entities cannot be found in it.
        s::MeshContext imported = mesh;
        imported.map = nullptr;
        imported.drawingRevision.reset();
        CHECK(has(s::buildModel(d, f.drawing, imported, load).issues, IssueKind::EntityNotMeshed));
    }
    SECTION("names that are not there")
    {
        s::Document& d = setup.document;
        const s::ItemId wrong =
            added(d, s::assign(d, s::BarAssignment{.id = {},
                                                   .targets = entities({f.bars.front()}),
                                                   .data = {.kind = c::ElementKind::Beam2D,
                                                            .material = "concrete",
                                                            .section = "box"}}));
        const auto issues = s::buildModel(d, f.drawing, meshOf(f.drawing), load).issues;
        CHECK(has(issues, IssueKind::UnknownMaterial, wrong));
        CHECK(has(issues, IssueKind::UnknownSection, wrong));
    }
    SECTION("a selection that is not a load case")
    {
        CHECK(has(
            s::buildModel(setup.document, f.drawing, meshOf(f.drawing), {.item = s::ItemId{999}})
                .issues,
            IssueKind::UnknownLoad));
    }
}

// REQ-010-13, 14: the state of targets.
TEST_CASE("the state of targets", "[setup][build]")
{
    Frame f;
    drawFrame(f);
    const s::MeshContext mesh = meshOf(f.drawing);
    CHECK(s::targetState(entities(f.bars), f.drawing, mesh) == s::TargetState::Valid);
    CHECK(s::targetState(entities({EntityId{999}}), f.drawing, mesh) ==
          s::TargetState::MissingEntity);
    s::Targets picked;
    picked.nodes = {1, 2};
    picked.meshGeneration = 1;
    CHECK(s::targetState(picked, f.drawing, mesh) == s::TargetState::Valid);
    picked.meshGeneration = 0;
    CHECK(s::targetState(picked, f.drawing, mesh) == s::TargetState::StaleSelection);
    s::Targets group;
    group.groups = {{.name = fem::mesher::entityGroupName(f.bars.front()), .dimension = 1}};
    CHECK(s::targetState(group, f.drawing, mesh) == s::TargetState::Valid);
    group.groups.front().name = "nope";
    CHECK(s::targetState(group, f.drawing, mesh) == s::TargetState::MissingGroup);
}
