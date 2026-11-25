// Measures the cost of the run control (NFR-011-03): the linear analysis of
// the bench_frame building with and without a control that never cancels
// and reports its progress the way the application will (into atomics read
// by a timer). Not part of ctest: run it on a Release build.
//
// Usage: bench_run_control [bays] [storeys] [rounds]
//   Defaults: the 10 x 10 bays and 40 storeys of bench_frame, 7 rounds of
//   each, interleaved; the medians are compared, and the difference must be
//   at most 2%.

#include "frame_building.hpp"

#include <fem/core/run_control.hpp>
#include <fem/core/static_analysis.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fem::perf::Clock;
using fem::perf::secondsSince;

double median(std::vector<double> values)
{
    std::ranges::sort(values);
    return values[values.size() / 2];
}

// One linear analysis, timed.
double timed(const fem::core::Model& model, const fem::core::StaticOptions& options)
{
    const auto start = Clock::now();
    const auto result = fem::core::runLinearStatic(model, options);
    const double seconds = secondsSince(start);
    if (!result) {
        throw std::runtime_error(result.error().message);
    }
    return seconds;
}

int run(int argc, char** argv)
{
    const int bays = argc > 1 ? std::stoi(argv[1]) : 10;
    const int storeys = argc > 2 ? std::stoi(argv[2]) : 40;
    const int rounds = argc > 3 ? std::stoi(argv[3]) : 7;
    if (bays < 1 || storeys < 1 || rounds < 1) {
        std::fputs("usage: bench_run_control [bays] [storeys] [rounds], each at least 1\n", stderr);
        return EXIT_FAILURE;
    }
    const fem::core::Model model = fem::perf::building(bays, storeys, false);
    std::printf("%d nodes, %d bars, %d rounds of each\n", model.mesh->nodeCount(),
                model.mesh->elementCount(), rounds);

    // What the application keeps: the flag, and the last stage and fraction.
    const std::atomic<bool> cancel{false};
    std::atomic<int> stage{0};
    std::atomic<double> fraction{0.0};
    std::atomic<std::size_t> reports{0};
    const fem::core::StaticOptions controlled{
        .threads = 0,
        .control = {.cancel = &cancel, .progress = [&](const fem::core::RunProgress& p) {
                        stage.store(static_cast<int>(p.stage), std::memory_order_relaxed);
                        fraction.store(p.fraction, std::memory_order_relaxed);
                        reports.fetch_add(1, std::memory_order_relaxed);
                    }}};
    const fem::core::StaticOptions plain{.threads = 0, .control = {}};

    (void)timed(model, plain); // warm up: caches, the thread pool
    std::vector<double> without;
    std::vector<double> with;
    for (int r = 0; r < rounds; ++r) {
        without.push_back(timed(model, plain));
        with.push_back(timed(model, controlled));
    }
    const double a = median(without);
    const double b = median(with);
    const double difference = (b - a) / a;
    std::printf("without control: median %.3f s (min %.3f)\n", a, std::ranges::min(without));
    std::printf("with control:    median %.3f s (min %.3f), %zu reports per analysis\n", b,
                std::ranges::min(with), reports.load() / static_cast<std::size_t>(rounds));
    std::printf("difference: %+.2f%% (NFR-011-03: at most 2%%) -> %s\n", 100.0 * difference,
                difference <= 0.02 ? "ok" : "ABOVE");
    return difference <= 0.02 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
