#pragma once

#include <cstdint>

namespace fem::setup {

// REQ-011-01 (design 011 §4): the analysis of the document and its options,
// for every case. Apart from analysis.hpp, which needs the document.
enum class AnalysisType : std::uint8_t { Linear, SecondOrder, Buckling };

// The defaults are those of the core (SecondOrderOptions, BucklingOptions).
struct AnalysisSettings
{
    AnalysisType type = AnalysisType::Linear;
    double tolerance = 1e-8; // second order
    int maxIterations = 50;  // second order
    int modes = 1;           // buckling
    friend bool operator==(const AnalysisSettings&, const AnalysisSettings&) = default;
};

} // namespace fem::setup
