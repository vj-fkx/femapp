#include "frame_document.hpp"

#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Feature 011, T07 (design §4, §5.3): REQ-011-01. The type and the options
// of the analysis, in the document. T08 (design §4, §5.4): REQ-011-02, 05,
// 10. runCase on the 2 x 2 frame of 010.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::Vec3;
using fem::setup::test::added;
using fem::setup::test::drawFrame;
using fem::setup::test::entities;
using fem::setup::test::Frame;
using fem::setup::test::meshOf;
using fem::setup::test::prepare;
using fem::setup::test::Setup;

// The same values at every node, to 1e-12 of the largest.
void same(const std::vector<Vec3>& a, const std::vector<Vec3>& b)
{
    REQUIRE(a.size() == b.size());
    double largest = 0.0;
    for (const Vec3& v : b) {
        largest = std::max(largest, v.norm());
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK((a[i] - b[i]).norm() <= 1e-12 * largest);
    }
}

void same(const c::StaticResult& a, const c::StaticResult& b)
{
    same(a.displacements, b.displacements);
    same(a.rotations, b.rotations);
    same(a.reactions, b.reactions);
    same(a.reactionMoments, b.reactionMoments);
    REQUIRE(a.axialForce.size() == b.axialForce.size());
    for (std::size_t e = 0; e < a.axialForce.size(); ++e) {
        const double x = a.axialForce[e];
        const double y = b.axialForce[e];
        if (std::isnan(y)) {
            CHECK(std::isnan(x)); // not a bar
        } else {
            CHECK(std::abs(x - y) <= 1e-12 * std::max(1.0, std::abs(y)));
        }
    }
}

void choose(s::Document& document, const s::AnalysisSettings& settings)
{
    document.apply(s::changeAnalysis(document, settings));
}

// The frame, its document and its mesh.
struct Prepared
{
    Frame frame;
    Setup setup;
    s::MeshContext mesh;
};

void prepared(Prepared& p)
{
    drawFrame(p.frame);
    prepare(p.setup, p.frame);
    p.mesh = meshOf(p.frame.drawing);
}

s::CaseOutcome run(const Prepared& p, s::ItemId item, const c::RunControl& control = {})
{
    return s::runCase(p.setup.document, p.frame.drawing, p.mesh, item, control);
}

c::Model built(const Prepared& p, s::ItemId item)
{
    s::BuildResult b = s::buildModel(p.setup.document, p.frame.drawing, p.mesh, {.item = item});
    REQUIRE(b.issues.empty());
    return std::move(b.model);
}

template <typename T>
T itemOfKind(const s::Document& document)
{
    const auto found = std::ranges::find_if(
        document.items(), [](const s::Item& i) { return std::holds_alternative<T>(i); });
    REQUIRE(found != document.items().end());
    return std::get<T>(*found);
}

// The result or the error of an outcome, which the test requires.
const s::AnalysisResult& resultOf(const s::CaseOutcome& outcome)
{
    REQUIRE(outcome.result.has_value());
    if (!outcome.result) {
        throw std::logic_error("no result");
    }
    return *outcome.result;
}

const c::Error& errorOf(const s::CaseOutcome& outcome)
{
    REQUIRE(outcome.error.has_value());
    if (!outcome.error) {
        throw std::logic_error("no error");
    }
    return *outcome.error;
}

void change(s::Document& document, const s::Item& item)
{
    const auto edit = s::changeItem(document, item);
    REQUIRE(edit.has_value());
    document.apply(*edit);
}

} // namespace

TEST_CASE("the options of the analysis start at the defaults of the core", "[setup][analysis]")
{
    const s::Document document;
    const s::AnalysisSettings settings = document.analysis();
    CHECK(settings.type == s::AnalysisType::Linear);
    const fem::core::SecondOrderOptions secondOrder;
    CHECK(settings.tolerance == secondOrder.tolerance);
    CHECK(settings.maxIterations == secondOrder.maxIterations);
    const fem::core::BucklingOptions buckling;
    CHECK(settings.modes == buckling.count);
    CHECK(settings == s::AnalysisSettings{});
}

TEST_CASE("a change of the analysis is undone and redone", "[setup][analysis]")
{
    s::Document document;
    const s::AnalysisSettings before = document.analysis();
    const s::AnalysisSettings after{
        .type = s::AnalysisType::Buckling, .tolerance = 1e-6, .maxIterations = 20, .modes = 4};
    const s::DocumentEdit edit = s::changeAnalysis(document, after);
    REQUIRE_FALSE(edit.empty());
    CHECK(edit.removed.empty());
    CHECK(edit.added.empty());
    CHECK_FALSE(edit.units);
    CHECK_FALSE(edit.activeCase);

    const auto revision = document.revision();
    document.apply(edit);
    CHECK(document.analysis() == after);
    CHECK(document.revision() != revision);
    document.revert(edit);
    CHECK(document.analysis() == before);
    CHECK(document.revision() == revision);
    document.apply(edit);
    CHECK(document.analysis() == after);
}

TEST_CASE("the same options are an empty edit", "[setup][analysis]")
{
    s::Document document;
    CHECK(s::changeAnalysis(document, document.analysis()).empty());
    const s::AnalysisSettings second{.type = s::AnalysisType::SecondOrder};
    document.apply(s::changeAnalysis(document, second));
    CHECK(s::changeAnalysis(document, second).empty());
    // One option alone is a change.
    s::AnalysisSettings tighter = second;
    tighter.tolerance = 1e-10;
    CHECK_FALSE(s::changeAnalysis(document, tighter).empty());
}

TEST_CASE("a change of units leaves the analysis alone", "[setup][analysis]")
{
    s::Document document;
    const s::AnalysisSettings settings{
        .type = s::AnalysisType::SecondOrder, .tolerance = 1e-6, .maxIterations = 80, .modes = 3};
    document.apply(s::changeAnalysis(document, settings));
    const s::DocumentEdit units = s::changeUnits(document, s::UnitSystem::NewtonMillimetre);
    CHECK_FALSE(units.analysis);
    document.apply(units);
    CHECK(document.analysis() == settings);
    document.revert(units);
    CHECK(document.analysis() == settings);
}

TEST_CASE("the three analyses of a case are those of the core", "[setup][analysis]")
{
    Prepared p;
    prepared(p);
    const s::ItemId item = p.setup.loadCase;
    const c::Model model = built(p, item);

    SECTION("linear")
    {
        const s::CaseOutcome outcome = run(p, item);
        REQUIRE(outcome.result.has_value());
        CHECK_FALSE(outcome.error.has_value());
        CHECK(outcome.issues.empty());
        const s::AnalysisResult& r = resultOf(outcome);
        CHECK(r.item == item);
        CHECK(r.settings == s::AnalysisSettings{});
        CHECK(r.state == s::AnalysedState{.documentRevision = p.setup.document.revision(),
                                          .drawingRevision = p.frame.drawing.revision(),
                                          .meshGeneration = p.mesh.generation});
        CHECK(r.mesh == p.mesh.mesh);
        CHECK(r.warnings.empty());
        // REQ-011-14: the three bases are the nodes held.
        std::vector<c::NodeIndex> held;
        for (const c::Restraint& restraint : model.restraints) {
            const auto nodes = c::resolve(*p.mesh.mesh, restraint.nodes);
            REQUIRE(nodes.has_value());
            held.insert(held.end(), nodes->begin(), nodes->end());
        }
        std::ranges::sort(held);
        held.erase(std::ranges::unique(held).begin(), held.end());
        CHECK(held.size() == 3);
        CHECK(r.supported == held);
        REQUIRE(std::holds_alternative<c::StaticResult>(r.data));
        const auto direct = c::runLinearStatic(model);
        REQUIRE(direct.has_value());
        same(std::get<c::StaticResult>(r.data), *direct);
    }
    SECTION("second order, with its options")
    {
        choose(p.setup.document,
               {.type = s::AnalysisType::SecondOrder, .tolerance = 1e-10, .maxIterations = 30});
        const s::CaseOutcome outcome = run(p, item);
        REQUIRE(outcome.result.has_value());
        CHECK(resultOf(outcome).settings.type == s::AnalysisType::SecondOrder);
        REQUIRE(std::holds_alternative<c::SecondOrderResult>(resultOf(outcome).data));
        const auto& r = std::get<c::SecondOrderResult>(resultOf(outcome).data);
        const auto direct =
            c::runSecondOrderStatic(model, {.base = {}, .tolerance = 1e-10, .maxIterations = 30});
        REQUIRE(direct.has_value());
        CHECK(r.iterations == direct->iterations);
        CHECK(r.change == direct->change);
        same(r.result, direct->result);
    }
    SECTION("buckling, with its modes")
    {
        choose(p.setup.document, {.type = s::AnalysisType::Buckling, .modes = 3});
        const s::CaseOutcome outcome = run(p, item);
        REQUIRE(outcome.result.has_value());
        REQUIRE(std::holds_alternative<c::BucklingResult>(resultOf(outcome).data));
        const auto& r = std::get<c::BucklingResult>(resultOf(outcome).data);
        const auto direct = c::runLinearBuckling(model, {.base = {}, .count = 3});
        REQUIRE(direct.has_value());
        REQUIRE(r.modes.size() == direct->modes.size());
        CHECK(r.modes.size() == 3);
        CHECK(r.note == direct->note);
        for (std::size_t m = 0; m < r.modes.size(); ++m) {
            CHECK(std::abs(r.modes[m].factor - direct->modes[m].factor) <=
                  1e-12 * direct->modes[m].factor);
            same(r.modes[m].displacements, direct->modes[m].displacements);
        }
    }
}

TEST_CASE("a combination is analysed as one case", "[setup][analysis]")
{
    Prepared p;
    prepared(p);
    s::Document& d = p.setup.document;
    const s::ItemId g =
        added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "G", .standard = {}, .category = {}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = g,
                                       .targets = entities({p.frame.bars[8], p.frame.bars[9]}),
                                       .action = c::DistributedLoad{.elements = {},
                                                                    .startValue = Vec3(0, -6, 0),
                                                                    .endValue = Vec3(0, -6, 0)}}));
    const s::ItemId uls =
        added(d, s::addItem(d, s::Combination{.id = {},
                                              .name = "ULS",
                                              .terms = {{g, 1.35}, {p.setup.loadCase, 1.5}}}));
    choose(d, {.type = s::AnalysisType::SecondOrder});
    const s::CaseOutcome outcome = run(p, uls);
    REQUIRE(outcome.result.has_value());
    CHECK(resultOf(outcome).item == uls);
    const auto direct = c::runSecondOrderStatic(built(p, uls));
    REQUIRE(direct.has_value());
    same(std::get<c::SecondOrderResult>(resultOf(outcome).data).result, direct->result);
}

TEST_CASE("errors of the check refuse a case", "[setup][analysis]")
{
    Prepared p;
    prepared(p);
    s::Document& d = p.setup.document;
    // The assignment names a material that is not there.
    auto bars = itemOfKind<s::BarAssignment>(d);
    bars.data.material = "aluminium";
    change(d, bars);
    const s::CaseOutcome outcome = run(p, p.setup.loadCase);
    CHECK_FALSE(outcome.result.has_value());
    CHECK_FALSE(outcome.error.has_value());
    CHECK(std::ranges::any_of(outcome.issues, [](const s::BuildIssue& i) {
        return i.severity == s::Severity::Error && i.kind == s::IssueKind::UnknownMaterial;
    }));

    // Without a mesh: NoMesh, and nothing runs.
    const s::CaseOutcome noMesh = s::runCase(d, p.frame.drawing, {}, p.setup.loadCase, {});
    CHECK_FALSE(noMesh.result.has_value());
    CHECK_FALSE(noMesh.error.has_value());
    REQUIRE(noMesh.issues.size() == 1);
    CHECK(noMesh.issues.front().kind == s::IssueKind::NoMesh);
}

TEST_CASE("a failure of the core comes back as its error", "[setup][analysis]")
{
    SECTION("a mechanism")
    {
        Prepared p;
        prepared(p);
        s::Document& d = p.setup.document;
        // The bases hold v alone: the frame slides in x.
        auto support = itemOfKind<s::SupportItem>(d);
        support.held = {};
        support.held[static_cast<std::size_t>(c::DofType::V)] = true;
        change(d, support);
        const s::CaseOutcome outcome = run(p, p.setup.loadCase);
        CHECK_FALSE(outcome.result.has_value());
        REQUIRE(outcome.error.has_value());
        CHECK_FALSE(errorOf(outcome).cancelled);
        CHECK(errorOf(outcome).message.starts_with("The structure is a mechanism"));
    }
    SECTION("buckling without a compressed bar")
    {
        // A single column, fixed at its base, pulled up at its top.
        Frame column;
        column.bars = {fem::setup::test::addLine(column.drawing, Vec3(0, 0, 0), Vec3(0, 3, 0))};
        column.bases = {fem::setup::test::addPoint(column.drawing, Vec3(0, 0, 0))};
        column.top = fem::setup::test::addPoint(column.drawing, Vec3(0, 3, 0));
        Setup setup;
        prepare(setup, column);
        s::Document& d = setup.document;
        auto load = itemOfKind<s::LoadItem>(d);
        load.action = c::NodalForce{.nodes = {}, .force = Vec3(0, 10, 0)};
        change(d, load);
        choose(d, {.type = s::AnalysisType::Buckling});
        const s::CaseOutcome outcome =
            s::runCase(d, column.drawing, meshOf(column.drawing), setup.loadCase, {});
        CHECK_FALSE(outcome.result.has_value());
        REQUIRE(outcome.error.has_value());
        CHECK(errorOf(outcome).message.starts_with("No bar is compressed"));
    }
}

TEST_CASE("a cancelled case has no result", "[setup][analysis]")
{
    Prepared p;
    prepared(p);
    const std::atomic<bool> cancel{true};
    const s::CaseOutcome outcome =
        run(p, p.setup.loadCase, c::RunControl{.cancel = &cancel, .progress = {}});
    CHECK_FALSE(outcome.result.has_value());
    REQUIRE(outcome.error.has_value());
    CHECK(errorOf(outcome).cancelled);
}

TEST_CASE("the warnings of the check come with the result", "[setup][analysis]")
{
    Prepared p;
    prepared(p);
    s::Document& d = p.setup.document;
    const s::ItemId truss =
        added(d, s::assign(d, s::BarAssignment{.id = {},
                                               .targets = entities({p.frame.bars.back()}),
                                               .data = {.elements = {},
                                                        .kind = c::ElementKind::Truss2D,
                                                        .material = "steel",
                                                        .section = "bar"}}));
    const s::CaseOutcome outcome = run(p, p.setup.loadCase);
    REQUIRE(outcome.result.has_value());
    const auto warned = [&](const std::vector<s::BuildIssue>& issues) {
        return std::ranges::any_of(issues, [&](const s::BuildIssue& i) {
            return i.severity == s::Severity::Warning && i.kind == s::IssueKind::TrussBuckling &&
                   i.item == truss;
        });
    };
    CHECK(warned(resultOf(outcome).warnings));
    CHECK(warned(outcome.issues));
}

namespace {

s::AnalysisResult resultFor(std::uint64_t item, double factor)
{
    c::BucklingResult data;
    data.modes.push_back(
        {.factor = factor, .displacements = {}, .rotations = {}, .warping = {}, .beams = {}});
    return {.item = s::ItemId{item},
            .settings = {.type = s::AnalysisType::Buckling},
            .state = {},
            .mesh = nullptr,
            .data = std::move(data),
            .warnings = {},
            .supported = {}};
}

const s::Extreme& extreme(const std::optional<s::Extreme>& value)
{
    REQUIRE(value.has_value());
    if (!value) {
        throw std::logic_error("no extreme");
    }
    return *value;
}

double factorOf(const s::AnalysisResult* r)
{
    REQUIRE(r != nullptr);
    if (r == nullptr) {
        throw std::logic_error("no result");
    }
    return std::get<c::BucklingResult>(r->data).modes.front().factor;
}

} // namespace

// T09: REQ-011-11.
TEST_CASE("one result per item, in the order of the items", "[setup][analysis][results]")
{
    s::ResultSet results;
    CHECK(results.all().empty());
    results.put(resultFor(7, 1.0));
    results.put(resultFor(3, 2.0));
    results.put(resultFor(5, 3.0));
    REQUIRE(results.all().size() == 3);
    CHECK(results.all()[0].item == s::ItemId{3});
    CHECK(results.all()[1].item == s::ItemId{5});
    CHECK(results.all()[2].item == s::ItemId{7});

    // A new analysis of an item replaces its result.
    results.put(resultFor(5, 4.0));
    CHECK(results.all().size() == 3);
    CHECK(factorOf(results.find(s::ItemId{5})) == 4.0);
    CHECK(results.find(s::ItemId{4}) == nullptr);

    results.erase(s::ItemId{3});
    results.erase(s::ItemId{9}); // not there: nothing
    REQUIRE(results.all().size() == 2);
    CHECK(results.find(s::ItemId{3}) == nullptr);
    CHECK(results.all()[0].item == s::ItemId{5});
    CHECK(factorOf(results.find(s::ItemId{7})) == 1.0);
}

// T09: REQ-011-12.
TEST_CASE("a result is current while the state analysed is the state now",
          "[setup][analysis][results]")
{
    Prepared p;
    prepared(p);
    s::Document& d = p.setup.document;
    const auto now = [&] {
        return s::AnalysedState{.documentRevision = d.revision(),
                                .drawingRevision = p.frame.drawing.revision(),
                                .meshGeneration = p.mesh.generation};
    };
    const s::AnalysisResult result = resultOf(run(p, p.setup.loadCase));
    CHECK(s::ResultSet::isCurrent(result, now()));

    SECTION("the document: undo back to it makes it current, redo does not")
    {
        const s::DocumentEdit edit =
            s::changeAnalysis(d, {.type = s::AnalysisType::Buckling, .modes = 2});
        d.apply(edit);
        CHECK_FALSE(s::ResultSet::isCurrent(result, now()));
        d.revert(edit);
        CHECK(s::ResultSet::isCurrent(result, now()));

        // Analysed after the edit, then undone and redone: the redo gives a
        // new revision, so that result is out of date although the
        // document is the same (the limit of design §5.3).
        d.apply(edit);
        const s::AnalysisResult edited = resultOf(run(p, p.setup.loadCase));
        CHECK(s::ResultSet::isCurrent(edited, now()));
        d.revert(edit);
        d.apply(edit);
        CHECK_FALSE(s::ResultSet::isCurrent(edited, now()));
        // The first one is still back one undo away.
        d.revert(edit);
        CHECK(s::ResultSet::isCurrent(result, now()));
    }
    SECTION("the drawing")
    {
        fem::geom::DrawingEdit edit;
        edit.added.push_back(fem::geom::Entity{
            .id = p.frame.drawing.nextId(),
            .shape = fem::geom::LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(-1, 0, 0)}});
        p.frame.drawing.apply(edit);
        CHECK_FALSE(s::ResultSet::isCurrent(result, now()));
        p.frame.drawing.revert(edit);
        CHECK(s::ResultSet::isCurrent(result, now()));
    }
    SECTION("the mesh")
    {
        p.mesh.generation += 1;
        CHECK_FALSE(s::ResultSet::isCurrent(result, now()));
    }
}

// T10: REQ-011-13. A cantilever of 3 m along x, fixed at x = 0, with the
// load (10, -20) of the frame at its tip: by hand, the tip moves
// v = P L^3 / (3 EI) and turns P L^2 / (2 EI); the base carries M = P L,
// V = P and N = 10.
TEST_CASE("the summary of a cantilever, by hand", "[setup][analysis][summary]")
{
    constexpr double kL = 3.0;
    Frame cantilever;
    cantilever.bars = {
        fem::setup::test::addLine(cantilever.drawing, Vec3(0, 0, 0), Vec3(kL, 0, 0))};
    cantilever.bases = {fem::setup::test::addPoint(cantilever.drawing, Vec3(0, 0, 0))};
    cantilever.top = fem::setup::test::addPoint(cantilever.drawing, Vec3(kL, 0, 0));
    Setup setup;
    prepare(setup, cantilever);
    const s::MeshContext mesh = meshOf(cantilever.drawing);
    const s::CaseOutcome outcome =
        s::runCase(setup.document, cantilever.drawing, mesh, setup.loadCase, {});
    const s::Summary summary = s::summarize(resultOf(outcome));

    const auto properties =
        c::sectionProperties(c::Section{.name = "bar", .shape = c::Rectangle{.b = 0.3, .h = 0.5}});
    REQUIRE(properties.has_value());
    const double ei = 2.0e8 * properties->inertiaZ;
    constexpr double kP = -20.0;
    const auto close = [](double a, double b) { return std::abs(a - b) <= 1e-9 * std::abs(b); };

    // The tip: its node id.
    const c::Mesh& m = *mesh.mesh;
    c::ExternalId tip = 0;
    for (c::NodeIndex n = 0; n < m.nodeCount(); ++n) {
        if ((m.nodeCoordinates(n) - Vec3(kL, 0, 0)).norm() < 1e-12) {
            tip = m.nodeId(n);
        }
    }
    REQUIRE(summary.displacement.has_value());
    CHECK(extreme(summary.displacement).id == tip);
    CHECK(extreme(summary.displacement).direction == 1);
    CHECK(close(extreme(summary.displacement).value, kP * kL * kL * kL / (3.0 * ei)));
    REQUIRE(summary.rotation.has_value());
    CHECK(extreme(summary.rotation).id == tip);
    CHECK(extreme(summary.rotation).direction == 2);
    CHECK(close(extreme(summary.rotation).value, kP * kL * kL / (2.0 * ei)));

    // The loads and the reactions balance.
    CHECK((summary.applied - Vec3(10, -20, 0)).norm() <= 1e-12 * 20.0);
    CHECK((summary.reactions + summary.applied).norm() <= 1e-9 * 20.0);

    // The forces: N, V and M (the moment at the base, the largest).
    REQUIRE(summary.barForces[0].has_value());
    CHECK(close(extreme(summary.barForces[0]).value, 10.0));
    CHECK(extreme(summary.barForces[0]).direction == 0);
    REQUIRE(summary.barForces[1].has_value());
    CHECK(close(std::abs(extreme(summary.barForces[1]).value), 20.0));
    REQUIRE(summary.barForces[5].has_value());
    CHECK(close(std::abs(extreme(summary.barForces[5]).value), 20.0 * kL));
    CHECK(extreme(summary.barForces[5]).direction == 5);
    // ... in the element at the base.
    bool atBase = false;
    for (c::ElementIndex e = 0; e < m.elementCount(); ++e) {
        if (m.elementId(e) == extreme(summary.barForces[5]).id) {
            for (const c::NodeIndex n : m.elementNodes(e)) {
                atBase = atBase || m.nodeCoordinates(n).norm() < 1e-12;
            }
        }
    }
    CHECK(atBase);
    CHECK(summary.iterations == 0);
    CHECK(summary.factors.empty());
}

TEST_CASE("the summary of the frame balances, and of the second order",
          "[setup][analysis][summary]")
{
    Prepared p;
    prepared(p);
    SECTION("linear")
    {
        const s::Summary summary = s::summarize(resultOf(run(p, p.setup.loadCase)));
        CHECK((summary.applied - Vec3(10, -20, 0)).norm() <= 1e-12 * 20.0);
        CHECK((summary.reactions + summary.applied).norm() <= 1e-9 * 20.0);
        CHECK(summary.iterations == 0);
    }
    SECTION("second order")
    {
        choose(p.setup.document, {.type = s::AnalysisType::SecondOrder});
        const s::AnalysisResult result = resultOf(run(p, p.setup.loadCase));
        const auto& data = std::get<c::SecondOrderResult>(result.data);
        const s::Summary summary = s::summarize(result);
        CHECK(summary.iterations == data.iterations);
        CHECK(summary.iterations > 1);
        CHECK(summary.change == data.change);
        CHECK((summary.reactions + summary.applied).norm() <= 1e-9 * 20.0);
        REQUIRE(summary.displacement.has_value());
        CHECK(summary.factors.empty());
    }
}

TEST_CASE("the summary of a buckling analysis", "[setup][analysis][summary]")
{
    Prepared p;
    prepared(p);
    choose(p.setup.document, {.type = s::AnalysisType::Buckling, .modes = 2});
    const s::AnalysisResult result = resultOf(run(p, p.setup.loadCase));
    const auto& data = std::get<c::BucklingResult>(result.data);
    const s::Summary summary = s::summarize(result);
    REQUIRE(summary.factors.size() == 2);
    CHECK(summary.factors[0] == data.modes[0].factor);
    CHECK(summary.factors[1] == data.modes[1].factor);
    CHECK(summary.factors[0] > 0.0);
    CHECK(summary.factors[0] <= summary.factors[1]);
    CHECK(summary.note.empty());
    // Nothing static in a mode.
    CHECK_FALSE(summary.displacement.has_value());
    CHECK_FALSE(summary.barForces[0].has_value());

    // Fewer factors than asked: the note of the core.
    s::AnalysisResult fewer = resultFor(4, 2.5);
    std::get<c::BucklingResult>(fewer.data).note = "Only 1 factor was found.";
    const s::Summary noted = s::summarize(fewer);
    CHECK(noted.factors == std::vector<double>{2.5});
    CHECK(noted.note == "Only 1 factor was found.");
}
