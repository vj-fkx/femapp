#include "frame_document.hpp"
#include "result_display_settings.hpp"
#include "results_model.hpp"

#include <fem/core/stability_analysis.hpp>
#include <fem/setup/editing.hpp>
#include <fem/setup/units.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// Feature 011, T17 (design §5.7): REQ-011-14, 15, NFR-011-04. The tables of
// the results over the frame of 010: rows, values, headers and the ids of
// a selection.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::app::ResultsModel;
using Table = ResultsModel::Table;

// The frame, analysed (linear by default).
struct Analysed
{
    fem::setup::test::Frame frame;
    fem::setup::test::Setup setup;
    s::MeshContext mesh;
    std::optional<s::AnalysisResult> result;
};

void analyse(Analysed& a, const s::AnalysisSettings& settings = {})
{
    fem::setup::test::drawFrame(a.frame);
    fem::setup::test::prepare(a.setup, a.frame);
    a.setup.document.apply(s::changeAnalysis(a.setup.document, settings));
    a.mesh = fem::setup::test::meshOf(a.frame.drawing);
    a.result = s::runCase(a.setup.document, a.frame.drawing, a.mesh, a.setup.loadCase, {}).result;
    REQUIRE(a.result.has_value());
}

const s::AnalysisResult& got(const Analysed& a)
{
    if (!a.result) {
        throw std::logic_error("no result");
    }
    return *a.result;
}

const c::Mesh& meshOf(const Analysed& a)
{
    if (!a.mesh.mesh) {
        throw std::logic_error("no mesh");
    }
    return *a.mesh.mesh;
}

// The elements with forces: the beams (the frame has no trusses; its
// other elements are the points at the bases).
std::vector<c::ElementIndex> barsOf(const c::StaticResult& r)
{
    std::vector<c::ElementIndex> bars;
    for (std::size_t e = 0; e < r.beams.size(); ++e) {
        if (r.beams[e]) {
            bars.push_back(static_cast<c::ElementIndex>(e));
        }
    }
    return bars;
}

double at(const ResultsModel& model, int row, int column)
{
    return model.data(model.index(row, column), Qt::DisplayRole).toDouble();
}

long long idAt(const ResultsModel& model, int row)
{
    return model.data(model.index(row, 0), Qt::DisplayRole).toLongLong();
}

std::string header(const ResultsModel& model, int column)
{
    return model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString().toStdString();
}

std::string bracketed(s::UnitSystem units, s::Quantity quantity)
{
    return " [" + s::unitLabel(units, quantity) + "]";
}

} // namespace

TEST_CASE("the tables of a static result", "[app][results]")
{
    Analysed a;
    analyse(a);
    const s::AnalysisResult& result = got(a);
    const c::Mesh& mesh = meshOf(a);
    const auto& r = std::get<c::StaticResult>(result.data);
    const s::UnitSystem units = a.setup.document.units();
    ResultsModel model;
    CHECK(model.rowCount() == 0);
    model.setResult(&result, units);
    CHECK(model.tables() ==
          std::vector<Table>{Table::Displacements, Table::Reactions, Table::BarForces});

    SECTION("displacements: every node, by its external id")
    {
        REQUIRE(model.rowCount() == mesh.nodeCount());
        CHECK(model.columnCount() == 7); // no warping in the frame
        CHECK(header(model, 0) == "Node");
        CHECK(header(model, 1) == "u" + bracketed(units, s::quantity::kLength));
        CHECK(header(model, 6) == "θz [rad]");
        for (const int row : {0, mesh.nodeCount() / 2, mesh.nodeCount() - 1}) {
            const auto n = static_cast<std::size_t>(row);
            CHECK(idAt(model, row) == mesh.nodeId(row));
            for (int k = 0; k < 3; ++k) {
                CHECK(at(model, row, 1 + k) == r.displacements[n][k]);
                CHECK(at(model, row, 4 + k) == r.rotations[n][k]);
            }
        }
        // Outside the table: nothing.
        CHECK_FALSE(model.data(model.index(model.rowCount(), 1), Qt::DisplayRole).isValid());
    }
    SECTION("reactions: the supported nodes alone")
    {
        model.setTable(Table::Reactions);
        REQUIRE(model.rowCount() == 3); // the three bases
        CHECK(header(model, 1) == "Rx" + bracketed(units, s::quantity::kForce));
        CHECK(header(model, 4) == "Mx" + bracketed(units, s::quantity::kMoment));
        for (int row = 0; row < 3; ++row) {
            const auto n =
                static_cast<std::size_t>(result.supported[static_cast<std::size_t>(row)]);
            CHECK(idAt(model, row) == mesh.nodeId(result.supported[static_cast<std::size_t>(row)]));
            for (int k = 0; k < 3; ++k) {
                CHECK(at(model, row, 1 + k) == r.reactions[n][k]);
                CHECK(at(model, row, 4 + k) == r.reactionMoments[n][k]);
            }
        }
    }
    SECTION("bar forces: both ends of every bar")
    {
        model.setTable(Table::BarForces);
        const std::vector<c::ElementIndex> bars = barsOf(r);
        REQUIRE_FALSE(bars.empty());
        REQUIRE(model.rowCount() == 2 * static_cast<int>(bars.size()));
        CHECK(model.columnCount() == 8);
        CHECK(header(model, 0) == "Element");
        CHECK(header(model, 1) == "End");
        CHECK(header(model, 2) == "N" + bracketed(units, s::quantity::kForce));
        CHECK(header(model, 7) == "Mz" + bracketed(units, s::quantity::kMoment));
        const c::ElementIndex last = bars.back();
        const auto& beam = r.beams[static_cast<std::size_t>(last)];
        REQUIRE(beam.has_value());
        if (!beam) {
            throw std::logic_error("not a beam");
        }
        const int start = 2 * (static_cast<int>(bars.size()) - 1);
        CHECK(idAt(model, start) == mesh.elementId(last));
        CHECK(idAt(model, start + 1) == mesh.elementId(last));
        CHECK(model.data(model.index(start, 1), Qt::DisplayRole).toString() == "start");
        CHECK(model.data(model.index(start + 1, 1), Qt::DisplayRole).toString() == "end");
        const std::array<double, 6> first{beam->start().n, beam->start().vy, beam->start().vz,
                                          beam->start().t, beam->start().my, beam->start().mz};
        const std::array<double, 6> second{beam->end().n, beam->end().vy, beam->end().vz,
                                           beam->end().t, beam->end().my, beam->end().mz};
        for (std::size_t k = 0; k < first.size(); ++k) {
            CHECK(at(model, start, 2 + static_cast<int>(k)) == first.at(k));
            CHECK(at(model, start + 1, 2 + static_cast<int>(k)) == second.at(k));
        }
    }
    SECTION("the buckling table of a static result is empty")
    {
        model.setTable(Table::Buckling);
        CHECK(model.rowCount() == 0);
        CHECK(model.modeCount() == 0);
    }
    SECTION("the headers follow the units of the document")
    {
        model.setResult(&result, s::UnitSystem::NewtonMillimetre);
        CHECK(header(model, 1) ==
              "u" + bracketed(s::UnitSystem::NewtonMillimetre, s::quantity::kLength));
        CHECK(bracketed(s::UnitSystem::NewtonMillimetre, s::quantity::kLength) !=
              bracketed(units, s::quantity::kLength));
    }
    SECTION("no result, no rows")
    {
        model.setResult(nullptr, units);
        CHECK(model.rowCount() == 0);
        CHECK(model.tables().empty());
    }
}

TEST_CASE("the table of a buckling result, mode by mode", "[app][results]")
{
    Analysed a;
    analyse(a, {.type = s::AnalysisType::Buckling, .modes = 3});
    const s::AnalysisResult& result = got(a);
    const c::Mesh& mesh = meshOf(a);
    const auto& r = std::get<c::BucklingResult>(result.data);
    ResultsModel model;
    model.setResult(&result, a.setup.document.units());
    CHECK(model.tables() == std::vector<Table>{Table::Buckling});
    model.setTable(Table::Buckling);
    REQUIRE(model.modeCount() == 3);
    CHECK(model.factor(1) == r.modes[1].factor);
    CHECK(header(model, 1) == "u"); // normalized: no unit

    model.setMode(1);
    CHECK(model.mode() == 1);
    REQUIRE(model.rowCount() == mesh.nodeCount());
    const int row = mesh.nodeCount() - 1;
    for (int k = 0; k < 3; ++k) {
        CHECK(at(model, row, 1 + k) == r.modes[1].displacements[static_cast<std::size_t>(row)][k]);
    }
    model.setMode(7); // past the last: the last
    CHECK(model.mode() == 2);
    // The static tables of a buckling result are empty.
    model.setTable(Table::Displacements);
    CHECK(model.rowCount() == 0);
}

TEST_CASE("selected rows give the nodes or the elements", "[app][results]")
{
    Analysed a;
    analyse(a);
    const s::AnalysisResult& result = got(a);
    const c::Mesh& mesh = meshOf(a);
    ResultsModel model;
    model.setResult(&result, a.setup.document.units());

    // REQ-011-15: nodes, unique and sorted.
    const fem::app::NamedIds nodes =
        model.idsOf({model.index(2, 0), model.index(0, 3), model.index(2, 5)});
    CHECK(nodes.elements.empty());
    CHECK(nodes.nodes == std::vector<c::ExternalId>{mesh.nodeId(0), mesh.nodeId(2)});

    // Both rows of a bar are one element.
    model.setTable(Table::BarForces);
    const fem::app::NamedIds bars =
        model.idsOf({model.index(2, 0), model.index(3, 4), model.index(model.rowCount(), 0)});
    CHECK(bars.nodes.empty());
    CHECK(bars.elements == std::vector<c::ExternalId>{
                               mesh.elementId(barsOf(std::get<c::StaticResult>(result.data))[1])});

    // The reactions: the supported nodes.
    model.setTable(Table::Reactions);
    const fem::app::NamedIds held = model.idsOf({model.index(0, 0)});
    CHECK(held.nodes == std::vector<c::ExternalId>{mesh.nodeId(result.supported.front())});
}

// REQ-012-19: the cells of the tables show their numbers as the viewport.
TEST_CASE("the text of the cells of the result tables", "[app][results]")
{
    Analysed a;
    analyse(a);
    ResultsModel model;
    model.setResult(&got(a), a.setup.document.units());
    model.setTable(Table::Displacements);
    REQUIRE(model.rowCount() > 0);
    for (int column = 1; column < model.columnCount(); ++column) {
        const QVariant value = model.data(model.index(0, column), Qt::DisplayRole);
        CHECK(fem::app::cellText(value) == fem::app::formatResult(value.toDouble()));
    }
    // The id column is not a double: its own text.
    const QVariant id = model.data(model.index(0, 0), Qt::DisplayRole);
    CHECK(fem::app::cellText(id) == id.toString());
}
