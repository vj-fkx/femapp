#include "beam_models.hpp"

#include <fem/core/expected.hpp>
#include <fem/core/run_control.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

using fem::core::DofType;
using fem::core::ElementKind;
using fem::core::Model;
using fem::core::RunControl;
using fem::core::RunProgress;
using fem::core::RunStage;
using fem::core::StaticResult;
using fem::core::test::TrussSpec;

namespace {

fem::core::ElementIds barIds(int count)
{
    fem::core::ElementIds ids;
    for (int j = 1; j <= count; ++j) {
        ids.ids.push_back(j);
    }
    return ids;
}

// A 2D portal frame: two columns and a beam with a span load, a lateral
// force at the top.
Model portal()
{
    const TrussSpec spec{.nodes = {Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0, 3, 0),
                                   Eigen::Vector3d(4, 3, 0), Eigen::Vector3d(4, 0, 0)},
                         .bars = {{0, 1}, {1, 2}, {2, 3}},
                         .lineGroups = {},
                         .pointGroups = {}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    fem::core::test::fixAll(model, {1, 4}, ElementKind::Beam2D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}},
                            .force = Eigen::Vector3d(5.0e3, 0, 0),
                            .moment = Eigen::Vector3d::Zero(),
                            .bimoment = 0.0});
    model.distributedLoads.push_back({.elements = fem::core::ElementIds{{2}},
                                      .startValue = Eigen::Vector3d(0, -2.0e3, 0),
                                      .endValue = Eigen::Vector3d(0, -6.0e3, 0)});
    return model;
}

// A 2D truss: a triangle on a pin and a roller, loaded at the apex.
Model truss()
{
    const TrussSpec spec{
        .nodes = {Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(4, 0, 0), Eigen::Vector3d(2, 1.5, 0)},
        .bars = {{0, 1}, {1, 2}, {2, 0}},
        .lineGroups = {},
        .pointGroups = {}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Truss2D);
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
    fem::core::test::restrain(model, {2}, {DofType::V});
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{3}},
                            .force = Eigen::Vector3d(1.0e3, -8.0e3, 0),
                            .moment = Eigen::Vector3d::Zero(),
                            .bimoment = 0.0});
    return model;
}

// A cantilever of `bars` beams with a uniform load over all of them and a
// concentrated load in the span of the first.
Model beam(int bars)
{
    TrussSpec spec;
    for (int i = 0; i <= bars; ++i) {
        spec.nodes.emplace_back(6.0 * i / bars, 0, 0);
    }
    for (int j = 0; j < bars; ++j) {
        spec.bars.push_back({j, j + 1});
    }
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    model.distributedLoads.push_back({.elements = barIds(bars),
                                      .startValue = Eigen::Vector3d(0, -1.0e3, 0),
                                      .endValue = Eigen::Vector3d(0, -1.0e3, 0)});
    model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                       .force = Eigen::Vector3d(0, -4.0e3, 0),
                                       .at = {.value = 0.3}});
    return model;
}

bool same(double a, double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same(const Eigen::Vector3d& a, const Eigen::Vector3d& b)
{
    return same(a.x(), b.x()) && same(a.y(), b.y()) && same(a.z(), b.z());
}

bool same(const fem::core::BeamForces& a, const fem::core::BeamForces& b)
{
    return same(a.n, b.n) && same(a.vy, b.vy) && same(a.vz, b.vz) && same(a.t, b.t) &&
           same(a.my, b.my) && same(a.mz, b.mz) && same(a.b, b.b);
}

// Every number of the two results, bit for bit (NaN equal to the same NaN).
void checkIdentical(const StaticResult& a, const StaticResult& b)
{
    REQUIRE(a.displacements.size() == b.displacements.size());
    for (std::size_t n = 0; n < a.displacements.size(); ++n) {
        CHECK(same(a.displacements[n], b.displacements[n]));
        CHECK(same(a.rotations[n], b.rotations[n]));
        CHECK(same(a.reactions[n], b.reactions[n]));
        CHECK(same(a.reactionMoments[n], b.reactionMoments[n]));
    }
    REQUIRE(a.axialForce.size() == b.axialForce.size());
    for (std::size_t e = 0; e < a.axialForce.size(); ++e) {
        CHECK(same(a.axialForce[e], b.axialForce[e]));
        CHECK(same(a.stress[e], b.stress[e]));
        REQUIRE(a.beams[e].has_value() == b.beams[e].has_value());
        if (a.beams[e] && b.beams[e]) {
            const fem::core::BeamResult& x = *a.beams[e];
            const fem::core::BeamResult& y = *b.beams[e];
            for (int k = 0; k <= 4; ++k) {
                const double s = x.length() * k / 4.0;
                CHECK(same(x.at(s).forces, y.at(s).forces));
                CHECK(same(x.at(s).displacement, y.at(s).displacement));
            }
        }
    }
}

StaticResult required(fem::core::Expected<StaticResult> result)
{
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    return std::move(result).value_or(StaticResult{});
}

// The progress reports of one run, in order (the callback may run on any
// thread).
struct Recorder
{
    std::mutex mutex;
    std::vector<RunProgress> seen;

    RunControl control(const std::atomic<bool>* cancel = nullptr)
    {
        return {.cancel = cancel, .progress = [this](const RunProgress& p) {
                    const std::scoped_lock lock(mutex);
                    seen.push_back(p);
                }};
    }
};

} // namespace

// REQ-011-09: without a flag and without a callback the control does nothing.
TEST_CASE("An empty RunControl is never cancelled and reports nowhere", "[core][run_control]")
{
    const RunControl control;
    CHECK_FALSE(control.cancelled());
    control.report(RunProgress{.stage = RunStage::Solution, .fraction = 0.5, .iteration = 0});

    const fem::core::StaticOptions options;
    CHECK_FALSE(options.control.cancelled());
    const fem::core::SecondOrderOptions second;
    CHECK_FALSE(second.base.control.cancelled());
    const fem::core::BucklingOptions buckling;
    CHECK_FALSE(buckling.base.control.cancelled());
}

// REQ-011-09: cancelled() follows the flag set by another party.
TEST_CASE("RunControl follows its cancel flag", "[core][run_control]")
{
    std::atomic<bool> flag{false};
    const RunControl control{.cancel = &flag, .progress = {}};
    CHECK_FALSE(control.cancelled());
    flag = true;
    CHECK(control.cancelled());
    flag = false;
    CHECK_FALSE(control.cancelled());
}

// REQ-011-09: report() forwards every progress to the callback.
TEST_CASE("RunControl forwards progress to its callback", "[core][run_control]")
{
    std::vector<RunProgress> seen;
    const RunControl control{.cancel = nullptr,
                             .progress = [&seen](const RunProgress& p) { seen.push_back(p); }};
    control.report(RunProgress{.stage = RunStage::Assembly, .fraction = 0.25, .iteration = 0});
    control.report(RunProgress{.stage = RunStage::Iteration, .fraction = -1.0, .iteration = 3});

    REQUIRE(seen.size() == 2);
    CHECK(seen[0].stage == RunStage::Assembly);
    CHECK(seen[0].fraction == 0.25);
    CHECK(seen[1].stage == RunStage::Iteration);
    CHECK(seen[1].iteration == 3);
}

// REQ-011-08: a cancellation is an Error marked as such; plain errors are not.
TEST_CASE("makeCancelled marks the error as a cancellation", "[core][run_control]")
{
    const fem::core::Expected<int> stopped = fem::core::makeCancelled();
    REQUIRE_FALSE(stopped.has_value());
    CHECK(stopped.error().cancelled);
    CHECK(stopped.error().message == "The analysis was cancelled.");

    const fem::core::Expected<int> failed = fem::core::makeError("Singular.");
    REQUIRE_FALSE(failed.has_value());
    CHECK_FALSE(failed.error().cancelled);
}
// REQ-011-07, 09, AC-4: a control that never cancels changes nothing in the
// numbers; the run without control is the reference.
TEST_CASE("A static analysis with a control gives the same numbers bit for bit",
          "[core][run_control]")
{
    for (const Model& model : {portal(), truss(), beam(40)}) {
        for (const int threads : {1, 0}) {
            const StaticResult reference =
                required(fem::core::runLinearStatic(model, {.threads = threads, .control = {}}));
            const std::atomic<bool> never{false};
            Recorder recorder;
            const StaticResult controlled = required(fem::core::runLinearStatic(
                model, {.threads = threads, .control = recorder.control(&never)}));
            checkIdentical(reference, controlled);
            CHECK_FALSE(recorder.seen.empty());
        }
    }
}

// REQ-011-07, 09: the stages in order, the assembly fraction growing to 1.
TEST_CASE("A static analysis reports its stages and the assembly fraction", "[core][run_control]")
{
    const Model model = beam(3000);
    Recorder recorder;
    REQUIRE(fem::core::runLinearStatic(model, {.threads = 1, .control = recorder.control()})
                .has_value());

    std::vector<RunStage> order;
    std::vector<double> assembly;
    for (const RunProgress& p : recorder.seen) {
        if (order.empty() || order.back() != p.stage) {
            order.push_back(p.stage);
        }
        if (p.stage == RunStage::Assembly) {
            assembly.push_back(p.fraction);
        }
        CHECK(p.iteration == 0);
        CHECK((p.fraction == -1.0 || (p.fraction >= 0.0 && p.fraction <= 1.0)));
    }
    CHECK(order == std::vector<RunStage>{RunStage::Assembly, RunStage::Factorization,
                                         RunStage::Solution, RunStage::Recovery});
    REQUIRE(assembly.size() > 1);
    for (std::size_t k = 1; k < assembly.size(); ++k) {
        CHECK(assembly[k] >= assembly[k - 1]);
    }
    CHECK(assembly.back() == 1.0);
}

// REQ-011-08, 09: cancelled before the start, at the first report of each
// stage, the analysis returns a cancellation.
TEST_CASE("A cancelled static analysis returns a cancellation", "[core][run_control]")
{
    const Model model = beam(3000);
    const auto cancelledAt = [&model](std::optional<RunStage> stage, int threads) {
        std::atomic<bool> flag{!stage.has_value()};
        const RunControl control{.cancel = &flag, .progress = [&flag, stage](const RunProgress& p) {
                                     if (stage && p.stage == *stage) {
                                         flag = true;
                                     }
                                 }};
        return fem::core::runLinearStatic(model, {.threads = threads, .control = control});
    };
    for (const int threads : {1, 0}) {
        for (const std::optional<RunStage> stage :
             {std::optional<RunStage>{}, std::optional<RunStage>{RunStage::Assembly},
              std::optional<RunStage>{RunStage::Factorization},
              std::optional<RunStage>{RunStage::Solution},
              std::optional<RunStage>{RunStage::Recovery}}) {
            const auto result = cancelledAt(stage, threads);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().cancelled);
            CHECK(result.error().message == "The analysis was cancelled.");
        }
    }
}
