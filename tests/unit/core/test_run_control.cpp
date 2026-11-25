#include <fem/core/expected.hpp>
#include <fem/core/run_control.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

using fem::core::RunControl;
using fem::core::RunProgress;
using fem::core::RunStage;

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
