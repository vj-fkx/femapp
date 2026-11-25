#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace fem::core {

// REQ-011-09: the stage a running analysis is in.
enum class RunStage : std::uint8_t { Assembly, Factorization, Solution, Recovery, Iteration };

struct RunProgress
{
    RunStage stage = RunStage::Assembly;
    double fraction = -1.0; // in [0, 1] within the stage; -1: unknown
    int iteration = 0;      // second order and buckling: 1, 2, ...; 0 outside them
};

// REQ-011-09: optional control of a running analysis (ADR 0011). The flag is
// set by another thread; `progress` may be called from any worker thread,
// often: it must be cheap and thread-safe (the app stores the numbers in
// atomics). Without a flag and without `progress` it does nothing.
struct RunControl
{
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const RunProgress&)> progress;

    [[nodiscard]] bool cancelled() const noexcept
    {
        return cancel != nullptr && cancel->load(std::memory_order_relaxed);
    }

    void report(const RunProgress& p) const
    {
        if (progress) {
            progress(p);
        }
    }
};

} // namespace fem::core
