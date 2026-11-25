// REQ-011-18 (design 011 §5.2a): the instability messages point to the node,
// the dof and the elements involved.

// GCC 13 at -O2 warns that the std::vector of the inactive NodeIds or
// ElementIds alternative "may be used uninitialized" when a temporary holding
// a GroupRef is moved into the model: a false positive of its variant
// analysis.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "beam_models.hpp"

#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::Model;
using fem::core::NodeIds;
using fem::core::test::TrussSpec;
using Vec = Eigen::Vector3d;

namespace {

std::string failure(const Model& model, int threads = 0)
{
    const auto result = fem::core::runLinearStatic(model, {.threads = threads, .control = {}});
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

// A square of 2D truss bars without its diagonal on two supports: nodes 1
// and 2 at the base, bars 1 (1-2), 2 (2-3), 3 (3-4), 4 (4-1). It sways:
// nodes 3 and 4 move in u, and every bar but the base moves.
Model square()
{
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(1, 0, 0), Vec(1, 1, 0), Vec(0, 1, 0)},
                         .bars = {{0, 1}, {1, 2}, {2, 3}, {3, 0}},
                         .lineGroups = {},
                         .pointGroups = {}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Truss2D);
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
    fem::core::test::restrain(model, {2}, {DofType::V});
    model.nodalForces.push_back(
        {.nodes = NodeIds{{3}}, .force = Vec(1.0e3, 0, 0), .moment = Vec::Zero(), .bimoment = 0.0});
    return model;
}

// A continuous beam of 10 bays (nodes 1 to 11 along x, beams 1 to 10) on a
// support at every node, and over the bay of nodes 6 and 7 a frame of three
// truss bars without a diagonal (nodes 12 over 7 and 13 over 6; bars 11
// 7-12, 12 12-13, 13 13-6). Only that frame can move.
Model beamWithLooseFrame()
{
    TrussSpec spec;
    for (int i = 0; i <= 10; ++i) {
        spec.nodes.emplace_back(i, 0, 0);
    }
    spec.nodes.emplace_back(6, 1, 0); // node 12
    spec.nodes.emplace_back(5, 1, 0); // node 13
    for (int i = 0; i < 10; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    spec.bars.push_back({6, 11});
    spec.bars.push_back({11, 12});
    spec.bars.push_back({12, 5});
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    model.assignments.front().elements = ElementIds{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    model.assignments.push_back({.elements = ElementIds{{11, 12, 13}},
                                 .kind = ElementKind::Truss2D,
                                 .material = "Steel",
                                 .section = "S"});
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::RZ});
    fem::core::test::restrain(model, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {DofType::V});
    model.nodalForces.push_back({.nodes = NodeIds{{12}},
                                 .force = Vec(0, -1.0e3, 0),
                                 .moment = Vec::Zero(),
                                 .bimoment = 0.0});
    return model;
}

} // namespace

// REQ-011-18: the mechanism names a node that moves and exactly the bars
// that move with it.
TEST_CASE("a mechanism names the node, the dof and the bars that move", "[core][instability]")
{
    const std::string message = failure(square());
    INFO(message);
    const bool nodeThree =
        message.starts_with("The structure is a mechanism: the node 3 in u can move");
    const bool nodeFour =
        message.starts_with("The structure is a mechanism: the node 4 in u can move");
    CHECK((nodeThree || nodeFour));
    CHECK(message.ends_with(" can move without resistance, with the elements 2, 3, 4 (a "
                            "support may be missing)."));
}

// REQ-011-18: in a large structure, only the loose part is named, wherever
// the zero pivot falls.
TEST_CASE("a local mechanism names only the elements of that part", "[core][instability]")
{
    const std::string message = failure(beamWithLooseFrame());
    INFO(message);
    const bool nodeTwelve =
        message.starts_with("The structure is a mechanism: the node 12 in u can move");
    const bool nodeThirteen =
        message.starts_with("The structure is a mechanism: the node 13 in u can move");
    CHECK((nodeTwelve || nodeThirteen));
    CHECK(message.ends_with(" can move without resistance, with the elements 11, 12, 13 (a "
                            "support may be missing)."));
}

// REQ-011-18: the same message with one thread and with all.
TEST_CASE("the mechanism message does not depend on the threads", "[core][instability]")
{
    CHECK(failure(square(), 1) == failure(square(), 0));
    CHECK(failure(beamWithLooseFrame(), 1) == failure(beamWithLooseFrame(), 0));
}

// REQ-011-18: a dof without any stiffness lists the elements at its node.
TEST_CASE("a dof without stiffness names the elements at its node", "[core][instability]")
{
    // A flat 3D truss: nothing holds node 3 in w (bars 2 and 3 end there).
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(4, 0, 0), Vec(2, 3, 0)},
                         .bars = {{0, 1}, {1, 2}, {2, 0}},
                         .lineGroups = {},
                         .pointGroups = {}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Truss3D);
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W});
    fem::core::test::restrain(model, {2}, {DofType::V, DofType::W});
    CHECK(failure(model) == "The structure is unstable: no bar gives stiffness to\n"
                            "- the node 3 in w (elements 2, 3)");
}

namespace {

// The ids in the "elements a, b, c" list after `marker` in `message`.
std::vector<int> listedAfter(const std::string& message, const std::string& marker)
{
    std::vector<int> ids;
    const auto at = message.find(marker);
    if (at == std::string::npos) {
        return ids;
    }
    std::size_t i = at + marker.size();
    while (i < message.size()) {
        std::size_t end = i;
        while (end < message.size() && message[end] >= '0' && message[end] <= '9') {
            ++end;
        }
        if (end == i) {
            break;
        }
        ids.push_back(std::stoi(message.substr(i, end - i)));
        if (message.compare(end, 2, ", ") != 0) {
            break;
        }
        i = end + 2;
    }
    return ids;
}

// A portal of 2D beams: columns of 4 bars each at x = 0 (bars 1 to 4, nodes
// 1 to 5) and x = 4 (bars 6 to 9, nodes 6 to 10, from the top down), a beam
// 5 (nodes 5 and 6) on top; fixed at nodes 1 and 10. Only the left column is
// loaded, at `factor` times the Euler load of a column held at both ends.
Model portal(double factor)
{
    TrussSpec spec;
    const double h = 4.0;
    for (int i = 0; i <= 4; ++i) {
        spec.nodes.emplace_back(0, h * i / 4, 0);
    }
    for (int i = 0; i <= 4; ++i) {
        spec.nodes.emplace_back(4, h * (4 - i) / 4, 0);
    }
    for (int i = 0; i < 9; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    fem::core::test::fixAll(model, {1, 10}, ElementKind::Beam2D);
    const double euler =
        std::numbers::pi * std::numbers::pi * fem::core::test::kE * fem::core::test::kIz / (h * h);
    model.nodalForces.push_back({.nodes = NodeIds{{5}},
                                 .force = Vec(1.0e3, -factor * euler, 0),
                                 .moment = Vec::Zero(),
                                 .bimoment = 0.0});
    return model;
}

} // namespace

// REQ-011-18: beyond its critical load, the second order names the lateral
// dof that moves the most and only the compressed elements that drive it.
TEST_CASE("an unstable second order names the dof and the compressed elements",
          "[core][instability]")
{
    const auto result = fem::core::runSecondOrderStatic(
        portal(3.0),
        {.base = {.threads = 0, .control = {}}, .tolerance = 1e-8, .maxIterations = 50});
    REQUIRE_FALSE(result.has_value());
    const std::string& message = result.error().message;
    INFO(message);
    CHECK(message.starts_with("The structure is unstable under these loads: the second-order "
                              "stiffness lost its stability at iteration "));
    CHECK(message.find(" in u moves the most, and the element") != std::string::npos);
    CHECK(message.ends_with(" it."));
    std::vector<int> ids = listedAfter(message, "the elements ");
    if (ids.empty()) {
        ids = listedAfter(message, "the element ");
    }
    REQUIRE_FALSE(ids.empty());
    for (const int id : ids) {
        CHECK(id >= 1);
        CHECK(id <= 4); // the loaded column only
    }

    // The same message with one thread.
    const auto sequential = fem::core::runSecondOrderStatic(
        portal(3.0),
        {.base = {.threads = 1, .control = {}}, .tolerance = 1e-8, .maxIterations = 50});
    REQUIRE_FALSE(sequential.has_value());
    CHECK(sequential.error().message == message);
}

// REQ-011-18: an instability inside a bar names the element and the local
// plane of its buckling.
TEST_CASE("an instability inside a bar names the element and the plane", "[core][instability]")
{
    const double l = 4.0;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(0, l, 0)},
                         .bars = {{0, 1}},
                         .lineGroups = {},
                         .pointGroups = {}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::RZ});
    fem::core::test::restrain(model, {2}, {DofType::U, DofType::RZ});
    for (const auto end : {fem::core::BarEnd::Start, fem::core::BarEnd::End}) {
        fem::core::EndConnection hinge{.elements = ElementIds{{1}}, .end = end};
        hinge.stiffness[5] = 0.0;
        model.endConnections.push_back(hinge);
    }
    const double euler =
        std::numbers::pi * std::numbers::pi * fem::core::test::kE * fem::core::test::kIz / (l * l);
    model.nodalForces.push_back({.nodes = NodeIds{{2}},
                                 .force = Vec(0, -1.05 * euler, 0),
                                 .moment = Vec::Zero(),
                                 .bimoment = 0.0});
    const auto result = fem::core::runSecondOrderStatic(model);
    REQUIRE_FALSE(result.has_value());
    INFO(result.error().message);
    CHECK(result.error().message.ends_with(
        "(inside the element 1, buckling in its local y direction)."));
}
