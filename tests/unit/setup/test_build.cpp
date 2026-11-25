#include <fem/core/mesh_builder.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
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
    added(d,
          s::addItem(d, s::MaterialItem{
                            .id = {},
                            .data = {.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3},
                            .standard = {},
                            .grade = {},
                            .properties = {}}));
    added(d, s::addItem(d, s::SectionItem{.id = {},
                                          .data = {.name = "bar",
                                                   .shape = c::Rectangle{.b = 0.3, .h = 0.5}}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = entities(f.bars),
                                           .data = {.elements = {},
                                                    .kind = c::ElementKind::Beam2D,
                                                    .material = "steel",
                                                    .section = "bar"}}));
    s::SupportItem fixed{.id = {}, .targets = entities(f.bases), .held = {}};
    fixed.held[static_cast<std::size_t>(c::DofType::U)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::V)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::RZ)] = true;
    added(d, s::assign(d, fixed));
    setup.loadCase = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "lateral", .standard = {}, .category = {}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = setup.loadCase,
                                       .targets = entities({f.top}),
                                       .action = c::NodalForce{.nodes = {},
                                                               .force = Vec3(10.0, -20.0, 0.0)}}));
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
    REQUIRE(builder.addGroup("bars", 1, bars).has_value());
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
        const EntityId extra = addLine(f.drawing, Vec3(20, 0, 0), Vec3(21, 0, 0));
        CHECK(has(s::buildModel(setup.document, f.drawing, mesh, load).issues,
                  IssueKind::MeshOutOfDate));
        // REQ-011-12: undoing that edit makes the mesh current again.
        const Entity* line = f.drawing.find(extra);
        REQUIRE(line != nullptr);
        f.drawing.revert(DrawingEdit{.removed = {}, .added = {*line}});
        CHECK_FALSE(has(s::buildModel(setup.document, f.drawing, mesh, load).issues,
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
                                                   .data = {.elements = {},
                                                            .kind = c::ElementKind::Beam2D,
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

namespace {

// The same analysis at every node, matching by external id (the same mesh).
void sameResults(const c::Model& a, const c::Model& b)
{
    const auto ra = c::runLinearStatic(a);
    const auto rb = c::runLinearStatic(b);
    if (!ra) {
        INFO(ra.error().message);
        REQUIRE(ra.has_value());
    }
    REQUIRE(rb.has_value());
    double largest = 0.0;
    double turned = 0.0;
    for (std::size_t n = 0; n < rb->displacements.size(); ++n) {
        largest = std::max(largest, rb->displacements[n].norm());
        turned = std::max(turned, rb->rotations[n].norm());
    }
    REQUIRE(largest > 0.0);
    for (std::size_t n = 0; n < ra->displacements.size(); ++n) {
        CHECK((ra->displacements[n] - rb->displacements[n]).norm() <= 1e-12 * largest);
        CHECK((ra->rotations[n] - rb->rotations[n]).norm() <= 1e-12 * std::max(1.0, turned));
        CHECK(std::abs(ra->warping[n] - rb->warping[n]) <= 1e-12 * std::max(1.0, turned));
    }
}

} // namespace

// AC-3: REQ-010-08 — every beam detail of the core through the document.
TEST_CASE("beam details prepared in the document", "[setup][build]")
{
    Drawing drawing;
    const EntityId first = addLine(drawing, Vec3(0, 0, 0), Vec3(3, 0, 0));
    const EntityId second = addLine(drawing, Vec3(3, 0, 0), Vec3(6, 0, 0));
    const EntityId root = addPoint(drawing, Vec3(0, 0, 0));
    const EntityId tip = addPoint(drawing, Vec3(6, 0, 0));
    const s::MeshContext mesh = meshOf(drawing);

    s::Document d;
    added(d,
          s::addItem(d, s::MaterialItem{
                            .id = {},
                            .data = {.name = "steel", .youngsModulus = 2.1e8, .poissonsRatio = 0.3},
                            .standard = {},
                            .grade = {},
                            .properties = {}}));
    added(d, s::addItem(d, s::SectionItem{
                               .id = {},
                               .data = {.name = "I",
                                        .shape = c::ISection{
                                            .h = 0.3, .bf = 0.15, .tf = 0.0107, .tw = 0.0071}}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = entities({first, second}),
                                           .data = {.elements = {},
                                                    .kind = c::ElementKind::Beam3D,
                                                    .material = "steel",
                                                    .section = "I",
                                                    .warping = true}}));
    c::EndConnection spring;
    spring.end = c::BarEnd::End;
    spring.stiffness[4] = 5e3; // My, at the end of the first bar
    added(d,
          s::assign(d, s::ConnectionItem{.id = {}, .targets = entities({first}), .data = spring}));
    added(d, s::assign(d, s::OffsetItem{.id = {},
                                        .targets = entities({second}),
                                        .data = {.elements = {}, .end = Vec3(0, 0, 0.05)}}));
    added(d, s::assign(d, s::FoundationItem{.id = {},
                                            .targets = entities({first, second}),
                                            .data = {.elements = {},
                                                     .translational = Vec3(0, 50, 80),
                                                     .torsional = 10}}));
    added(d,
          s::assign(d, s::WarpingEndItem{.id = {},
                                         .targets = entities({first}),
                                         .data = {.elements = {},
                                                  .end = c::BarEnd::Start,
                                                  .condition = c::WarpingCondition::Restrained}}));
    s::SupportItem fixed{.id = {}, .targets = entities({root}), .held = {}};
    for (const auto dof : {c::DofType::U, c::DofType::V, c::DofType::W, c::DofType::RX,
                           c::DofType::RY, c::DofType::RZ}) {
        fixed.held[static_cast<std::size_t>(dof)] = true;
    }
    added(d, s::assign(d, fixed));
    const s::ItemId loadCase = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "tip", .standard = {}, .category = {}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = loadCase,
                                       .targets = entities({tip}),
                                       .action = c::NodalForce{.nodes = {},
                                                               .force = Vec3(0, 1, -2),
                                                               .moment = Vec3(0.3, 0, 0)}}));
    const s::BuildResult built = s::buildModel(d, drawing, mesh, {.item = loadCase});
    for (const s::BuildIssue& issue : built.issues) {
        INFO(static_cast<int>(issue.kind) << " " << issue.detail);
        CHECK(issue.severity != s::Severity::Error);
    }

    // By hand, on the same mesh: the first bar is element 1, the second 2;
    // the root is node 1, the tip node 3 (009 numbering).
    c::Model hand;
    hand.mesh = mesh.mesh;
    hand.materials.push_back({.name = "steel", .youngsModulus = 2.1e8, .poissonsRatio = 0.3});
    hand.sections.push_back(
        {.name = "I", .shape = c::ISection{.h = 0.3, .bf = 0.15, .tf = 0.0107, .tw = 0.0071}});
    hand.assignments.push_back({.elements = c::ElementIds{{1, 2}},
                                .kind = c::ElementKind::Beam3D,
                                .material = "steel",
                                .section = "I",
                                .warping = true});
    c::EndConnection handSpring = spring;
    handSpring.elements = c::ElementIds{{1}};
    hand.endConnections.push_back(handSpring);
    hand.rigidOffsets.push_back({.elements = c::ElementIds{{2}}, .end = Vec3(0, 0, 0.05)});
    hand.foundations.push_back(
        {.elements = c::ElementIds{{1, 2}}, .translational = Vec3(0, 50, 80), .torsional = 10});
    hand.warpingEnds.push_back({.elements = c::ElementIds{{1}},
                                .end = c::BarEnd::Start,
                                .condition = c::WarpingCondition::Restrained});
    for (const auto dof : {c::DofType::U, c::DofType::V, c::DofType::W, c::DofType::RX,
                           c::DofType::RY, c::DofType::RZ}) {
        hand.restraints.push_back({.nodes = c::NodeIds{{1}}, .dof = dof, .value = 0.0});
    }
    hand.nodalForces.push_back(
        {.nodes = c::NodeIds{{3}}, .force = Vec3(0, 1, -2), .moment = Vec3(0.3, 0, 0)});
    sameResults(built.model, hand);
}

// AC-4: REQ-010-12, 13 — an imported mesh, by group and by picked items.
TEST_CASE("targets on an imported mesh", "[setup][build]")
{
    // A cantilever of two bars, with point groups at the root and the tip.
    c::MeshBuilder builder;
    REQUIRE(builder.addNode(10, Vec3(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(20, Vec3(2, 0, 0)).has_value());
    REQUIRE(builder.addNode(30, Vec3(4, 0, 0)).has_value());
    const std::array<c::ExternalId, 2> a{10, 20};
    const std::array<c::ExternalId, 2> b{20, 30};
    const std::array<c::ExternalId, 1> r{10};
    const auto e1 = builder.addElement(c::ElementType::Line2, 1, a);
    const auto e2 = builder.addElement(c::ElementType::Line2, 2, b);
    const auto p = builder.addElement(c::ElementType::Point1, 3, r);
    REQUIRE((e1 && e2 && p));
    REQUIRE(builder.addGroup("beam", 1, {*e1, *e2}).has_value());
    REQUIRE(builder.addGroup("root", 0, {*p}).has_value());
    auto built = builder.build();
    REQUIRE(built.has_value());
    s::MeshContext mesh;
    mesh.mesh = std::make_shared<const c::Mesh>(std::move(*built));
    mesh.generation = 3; // no map, no drawing revision: imported

    const Drawing drawing;
    s::Document d;
    added(d,
          s::addItem(d, s::MaterialItem{
                            .id = {},
                            .data = {.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3},
                            .standard = {},
                            .grade = {},
                            .properties = {}}));
    added(d, s::addItem(d, s::SectionItem{
                               .id = {},
                               .data = {.name = "R", .shape = c::Rectangle{.b = 0.2, .h = 0.4}}}));
    s::Targets beam;
    beam.groups = {{.name = "beam", .dimension = 1}};
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = beam,
                                           .data = {.elements = {},
                                                    .kind = c::ElementKind::Beam2D,
                                                    .material = "steel",
                                                    .section = "R"}}));
    s::Targets root;
    root.groups = {{.name = "root", .dimension = 0}};
    s::SupportItem fixed{.id = {}, .targets = root, .held = {}};
    fixed.held[static_cast<std::size_t>(c::DofType::U)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::V)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::RZ)] = true;
    added(d, s::assign(d, fixed));
    const s::ItemId loadCase = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "tip", .standard = {}, .category = {}}));
    s::Targets tip;
    tip.nodes = {30};
    tip.meshGeneration = 3;
    const s::ItemId load = added(
        d,
        s::addItem(d, s::LoadItem{.id = {},
                                  .loadCase = loadCase,
                                  .targets = tip,
                                  .action = c::NodalForce{.nodes = {}, .force = Vec3(0, -5, 0)}}));
    // A distributed load on a picked element.
    s::Targets second;
    second.elements = {2};
    second.meshGeneration = 3;
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = loadCase,
                                       .targets = second,
                                       .action = c::DistributedLoad{.elements = {},
                                                                    .startValue = Vec3(0, -1, 0),
                                                                    .endValue = Vec3(0, -1, 0)}}));

    const s::BuildResult result = s::buildModel(d, drawing, mesh, {.item = loadCase});
    CHECK(result.issues.empty());
    c::Model hand;
    hand.mesh = mesh.mesh;
    hand.materials.push_back({.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3});
    hand.sections.push_back({.name = "R", .shape = c::Rectangle{.b = 0.2, .h = 0.4}});
    hand.assignments.push_back({.elements = c::GroupRef{"beam"},
                                .kind = c::ElementKind::Beam2D,
                                .material = "steel",
                                .section = "R"});
    // Field by field: g++ 13 at -O2 takes the variant of a temporary
    // initializer for uninitialized (-Wmaybe-uninitialized).
    for (const auto dof : {c::DofType::U, c::DofType::V, c::DofType::RZ}) {
        c::Restraint restraint;
        restraint.nodes = c::GroupRef{"root"};
        restraint.dof = dof;
        hand.restraints.push_back(restraint);
    }
    hand.nodalForces.push_back({.nodes = c::NodeIds{{30}}, .force = Vec3(0, -5, 0)});
    hand.distributedLoads.push_back(
        {.elements = c::ElementIds{{2}}, .startValue = Vec3(0, -1, 0), .endValue = Vec3(0, -1, 0)});
    sameResults(result.model, hand);

    // A new mesh: the picked load and element are stale and left out.
    s::MeshContext next = mesh;
    next.generation = 4;
    const s::BuildResult stale = s::buildModel(d, drawing, next, {.item = loadCase});
    CHECK(has(stale.issues, IssueKind::StaleSelection, load));
    CHECK(stale.model.nodalForces.empty());
    CHECK(stale.model.distributedLoads.empty());
    // Entity targets on an imported mesh cannot be found.
    added(d, s::assign(d, s::OffsetItem{.id = {}, .targets = entities({EntityId{1}}), .data = {}}));
    CHECK(
        has(s::buildModel(d, drawing, mesh, {.item = loadCase}).issues, IssueKind::MissingEntity));
    // A group that is not in the mesh.
    s::Targets nope;
    nope.groups = {{.name = "nope", .dimension = 1}};
    const s::ItemId missing =
        added(d, s::assign(d, s::FoundationItem{.id = {}, .targets = nope, .data = {}}));
    CHECK(has(s::buildModel(d, drawing, mesh, {.item = loadCase}).issues, IssueKind::MissingGroup,
              missing));
}

// AC-5: REQ-010-15, 16, 20 — every action of the core in one load case.
TEST_CASE("every action of a load case", "[setup][build][actions]")
{
    Drawing drawing;
    const EntityId first = addLine(drawing, Vec3(0, 0, 0), Vec3(3, 0, 0));
    const EntityId second = addLine(drawing, Vec3(3, 0, 0), Vec3(6, 0, 0));
    const EntityId root = addPoint(drawing, Vec3(0, 0, 0));
    const EntityId tip = addPoint(drawing, Vec3(6, 0, 0));
    // The warping dof is where the two collinear bars meet.
    const EntityId middle = addPoint(drawing, Vec3(3, 0, 0));
    const s::MeshContext mesh = meshOf(drawing);

    s::Document d;
    added(d, s::addItem(d, s::MaterialItem{.id = {},
                                           .data = {.name = "steel",
                                                    .youngsModulus = 2.1e8,
                                                    .poissonsRatio = 0.3,
                                                    .density = 7.85,
                                                    .thermalExpansion = 1.2e-5},
                                           .standard = {},
                                           .grade = {},
                                           .properties = {}}));
    const c::ISection shape{.h = 0.3, .bf = 0.15, .tf = 0.0107, .tw = 0.0071};
    added(d, s::addItem(d, s::SectionItem{.id = {}, .data = {.name = "I", .shape = shape}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = entities({first, second}),
                                           .data = {.elements = {},
                                                    .kind = c::ElementKind::Beam3D,
                                                    .material = "steel",
                                                    .section = "I",
                                                    .warping = true}}));
    s::SupportItem fixed{.id = {}, .targets = entities({root}), .held = {}};
    for (const auto dof : {c::DofType::U, c::DofType::V, c::DofType::W, c::DofType::RX,
                           c::DofType::RY, c::DofType::RZ}) {
        fixed.held[static_cast<std::size_t>(dof)] = true;
    }
    added(d, s::assign(d, fixed));
    const s::ItemId all = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "all", .standard = {}, .category = {}}));
    const auto load = [&](s::Targets targets, s::Action action) {
        added(d, s::addItem(d, s::LoadItem{.id = {},
                                           .loadCase = all,
                                           .targets = std::move(targets),
                                           .action = std::move(action)}));
    };
    load(entities({tip}),
         c::NodalForce{.nodes = {}, .force = Vec3(1, 2, -3), .moment = Vec3(0.4, 0.5, 0.6)});
    load(entities({middle}), c::NodalForce{.nodes = {}, .bimoment = 0.07});
    load(entities({first}), c::DistributedLoad{.elements = {},
                                               .axes = c::Axes::Local,
                                               .startValue = Vec3(0, 0.5, -1.0),
                                               .endValue = Vec3(0, 1.5, -2.0),
                                               .from = {.value = 0.5, .fraction = false},
                                               .to = {.value = 0.9, .fraction = true},
                                               .startTorque = 0.1,
                                               .endTorque = 0.2,
                                               .point = Eigen::Vector2d(0.15, 0.0)});
    load(entities({second}), c::ConcentratedLoad{.elements = {},
                                                 .force = Vec3(0, -4, 1),
                                                 .moment = Vec3(0.2, 0, 0),
                                                 .at = {.value = 0.25, .fraction = true}});
    load({}, s::SelfWeight{.acceleration = Vec3(0, 0, -9.81)});
    load(entities({second}), c::TemperatureChange{.elements = {}, .deltaT = 20.0});
    load(entities({first}),
         c::TemperatureGradient{.elements = {}, .alongY = 15.0, .alongZ = -10.0});
    load(entities({second}), c::InitialStrain{.elements = {}, .strain = 2e-4});
    load(entities({root}), s::Settlement{.dof = c::DofType::W, .value = -0.002});

    const s::BuildResult built = s::buildModel(d, drawing, mesh, {.item = all});
    CHECK(built.issues.empty());

    c::Model hand;
    hand.mesh = mesh.mesh;
    hand.materials.push_back({.name = "steel",
                              .youngsModulus = 2.1e8,
                              .poissonsRatio = 0.3,
                              .density = 7.85,
                              .thermalExpansion = 1.2e-5});
    hand.sections.push_back({.name = "I", .shape = shape});
    hand.assignments.push_back({.elements = c::ElementIds{{1, 2}},
                                .kind = c::ElementKind::Beam3D,
                                .material = "steel",
                                .section = "I",
                                .warping = true});
    for (const auto dof :
         {c::DofType::U, c::DofType::V, c::DofType::RX, c::DofType::RY, c::DofType::RZ}) {
        hand.restraints.push_back({.nodes = c::NodeIds{{1}}, .dof = dof, .value = 0.0});
    }
    hand.restraints.push_back({.nodes = c::NodeIds{{1}}, .dof = c::DofType::W, .value = -0.002});
    hand.nodalForces.push_back(
        {.nodes = c::NodeIds{{3}}, .force = Vec3(1, 2, -3), .moment = Vec3(0.4, 0.5, 0.6)});
    hand.nodalForces.push_back({.nodes = c::NodeIds{{2}}, .bimoment = 0.07});
    hand.distributedLoads.push_back({.elements = c::ElementIds{{1}},
                                     .axes = c::Axes::Local,
                                     .startValue = Vec3(0, 0.5, -1.0),
                                     .endValue = Vec3(0, 1.5, -2.0),
                                     .from = {.value = 0.5, .fraction = false},
                                     .to = {.value = 0.9, .fraction = true},
                                     .startTorque = 0.1,
                                     .endTorque = 0.2,
                                     .point = Eigen::Vector2d(0.15, 0.0)});
    hand.concentratedLoads.push_back({.elements = c::ElementIds{{2}},
                                      .force = Vec3(0, -4, 1),
                                      .moment = Vec3(0.2, 0, 0),
                                      .at = {.value = 0.25, .fraction = true}});
    hand.gravity = Vec3(0, 0, -9.81);
    hand.temperatureChanges.push_back({.elements = c::ElementIds{{2}}, .deltaT = 20.0});
    hand.temperatureGradients.push_back(
        {.elements = c::ElementIds{{1}}, .alongY = 15.0, .alongZ = -10.0});
    hand.initialStrains.push_back({.elements = c::ElementIds{{2}}, .strain = 2e-4});
    sameResults(built.model, hand);
}

// AC-5: REQ-010-17 — 1.4 G + 1.5 Q as the sum of the actions, also in the
// second order, which does not superpose.
TEST_CASE("a combination is the sum of its actions", "[setup][build][actions]")
{
    Frame f;
    drawFrame(f);
    Setup setup;
    prepare(setup, f); // "lateral": 10, -20 at the top corner
    s::Document& d = setup.document;
    // G: the frame's own weight and a load on the upper beams.
    {
        s::MaterialItem steel = std::get<s::MaterialItem>(d.items().front());
        steel.data.density = 7.85;
        REQUIRE(s::changeItem(d, steel).has_value());
        d.apply(*s::changeItem(d, steel));
    }
    const s::ItemId g =
        added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "G", .standard = {}, .category = {}}));
    added(d,
          s::addItem(d, s::LoadItem{.id = {},
                                    .loadCase = g,
                                    .targets = {},
                                    .action = s::SelfWeight{.acceleration = Vec3(0, -9.81, 0)}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = g,
                                       .targets = entities({f.bars[8], f.bars[9]}),
                                       .action = c::DistributedLoad{.elements = {},
                                                                    .startValue = Vec3(0, -6, 0),
                                                                    .endValue = Vec3(0, -6, 0)}}));
    const s::ItemId uls = added(
        d, s::addItem(d, s::Combination{
                             .id = {}, .name = "ULS", .terms = {{g, 1.4}, {setup.loadCase, 1.5}}}));
    const s::BuildResult built = s::buildModel(d, f.drawing, meshOf(f.drawing), {.item = uls});
    CHECK(built.issues.empty());
    CHECK(built.model.gravity == Vec3(0, -1.4 * 9.81, 0));

    // By hand: the same frame with the actions already summed.
    c::Model hand = handFrame();
    hand.materials.front().density = 7.85;
    hand.gravity = Vec3(0, -1.4 * 9.81, 0);
    hand.nodalForces.front().force *= 1.5;
    // The upper beams by hand are elements 508 and 509 (009 T10 order).
    hand.distributedLoads.push_back({.elements = c::ElementIds{{508, 509}},
                                     .startValue = Vec3(0, -6 * 1.4, 0),
                                     .endValue = Vec3(0, -6 * 1.4, 0)});
    sameAnalysis(built.model, hand);

    const auto a = c::runSecondOrderStatic(built.model);
    const auto b = c::runSecondOrderStatic(hand);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const c::Mesh& ma = *built.model.mesh;
    const c::Mesh& mb = *hand.mesh;
    double largest = 0.0;
    for (const Vec3& u : b->result.displacements) {
        largest = std::max(largest, u.norm());
    }
    for (c::NodeIndex n = 0; n < ma.nodeCount(); ++n) {
        c::NodeIndex m = 0;
        while ((mb.nodeCoordinates(m) - ma.nodeCoordinates(n)).norm() > 1e-12) {
            ++m;
        }
        CHECK((a->result.displacements[static_cast<std::size_t>(n)] -
               b->result.displacements[static_cast<std::size_t>(m)])
                  .norm() <= 1e-10 * largest);
    }
}

// REQ-010-15, 17: settlements add up by node and dof; one where nothing is
// held is an issue.
TEST_CASE("settlements in a combination", "[setup][build][actions]")
{
    Frame f;
    drawFrame(f);
    Setup setup;
    prepare(setup, f);
    s::Document& d = setup.document;
    const s::ItemId one = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "one", .standard = {}, .category = {}}));
    const s::ItemId two = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "two", .standard = {}, .category = {}}));
    added(d, s::addItem(
                 d, s::LoadItem{.id = {},
                                .loadCase = one,
                                .targets = entities({f.bases[1]}),
                                .action = s::Settlement{.dof = c::DofType::V, .value = -0.01}}));
    added(d, s::addItem(
                 d, s::LoadItem{.id = {},
                                .loadCase = two,
                                .targets = entities({f.bases[1]}),
                                .action = s::Settlement{.dof = c::DofType::V, .value = -0.004}}));
    const s::ItemId both = added(
        d,
        s::addItem(d, s::Combination{.id = {}, .name = "both", .terms = {{one, 1.0}, {two, 2.0}}}));
    const s::BuildResult built = s::buildModel(d, f.drawing, meshOf(f.drawing), {.item = both});
    CHECK(built.issues.empty());
    // The middle base is node 4 (009 numbering): V at -0.01 - 2 * 0.004.
    const auto settled = std::ranges::find_if(built.model.restraints, [](const c::Restraint& r) {
        return r.dof == c::DofType::V && r.value != 0.0;
    });
    REQUIRE(settled != built.model.restraints.end());
    CHECK(std::abs(settled->value - (-0.01 - 2.0 * 0.004)) <= 1e-15);
    CHECK(std::get<c::NodeIds>(settled->nodes).ids == std::vector<c::ExternalId>{4});

    // A settlement where nothing is held.
    const s::ItemId wrong = added(
        d,
        s::addItem(d, s::LoadItem{.id = {},
                                  .loadCase = one,
                                  .targets = entities({f.top}),
                                  .action = s::Settlement{.dof = c::DofType::U, .value = 0.001}}));
    CHECK(has(s::buildModel(d, f.drawing, meshOf(f.drawing), {.item = one}).issues,
              IssueKind::SettlementNotHeld, wrong));
}

// REQ-010-10, 21: the check lists the problems of the build (with their
// item), those of core::validate (without one) and the truss warning.
TEST_CASE("the check of a model", "[setup][build][check]")
{
    Frame f;
    drawFrame(f);
    Setup setup;
    prepare(setup, f);
    s::Document& d = setup.document;
    const s::LoadSelection load{.item = setup.loadCase};

    SECTION("a complete model has nothing to report")
    {
        CHECK(s::checkModel(d, f.drawing, meshOf(f.drawing), load).empty());
    }
    SECTION("a bar without an assignment, from the core")
    {
        // The last beam goes back to no assignment.
        const auto& bars =
            std::get<s::BarAssignment>(*std::ranges::find_if(d.items(), [](const s::Item& i) {
                return std::holds_alternative<s::BarAssignment>(i);
            }));
        s::BarAssignment fewer = bars;
        fewer.targets.entities.pop_back();
        const auto change = s::changeItem(d, fewer);
        REQUIRE(change.has_value());
        d.apply(*change);
        const auto issues = s::checkModel(d, f.drawing, meshOf(f.drawing), load);
        const auto core = std::ranges::find_if(
            issues, [](const s::BuildIssue& i) { return i.kind == IssueKind::Core; });
        REQUIRE(core != issues.end());
        CHECK(core->severity == s::Severity::Error);
        CHECK_FALSE(core->item.has_value());
        CHECK(core->detail.find("without an assignment") != std::string::npos);
    }
    SECTION("a truss is warned about")
    {
        const s::ItemId truss =
            added(d, s::assign(d, s::BarAssignment{.id = {},
                                                   .targets = entities({f.bars.back()}),
                                                   .data = {.elements = {},
                                                            .kind = c::ElementKind::Truss2D,
                                                            .material = "steel",
                                                            .section = "bar"}}));
        const auto issues = s::checkModel(d, f.drawing, meshOf(f.drawing), load);
        const auto warning = std::ranges::find_if(
            issues, [](const s::BuildIssue& i) { return i.kind == IssueKind::TrussBuckling; });
        REQUIRE(warning != issues.end());
        CHECK(warning->severity == s::Severity::Warning);
        CHECK(warning->item == truss);
    }
    SECTION("problems of the build come with their item, and stop the core check without a mesh")
    {
        const s::ItemId gone =
            added(d, s::assign(d, s::OffsetItem{
                                      .id = {}, .targets = entities({EntityId{999}}), .data = {}}));
        CHECK(has(s::checkModel(d, f.drawing, meshOf(f.drawing), load), IssueKind::MissingEntity,
                  gone));
        const auto noMesh = s::checkModel(d, f.drawing, {}, load);
        REQUIRE(noMesh.size() == 1);
        CHECK(noMesh.front().kind == IssueKind::NoMesh);
    }
}
