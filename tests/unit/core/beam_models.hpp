#pragma once

// Beam models for the tests of feature 008: a steel material, a generic
// section and helpers for supports and loads. Bars come from bar_meshes.hpp
// (node i gets id i + 1, bar j gets id j + 1).

#include "bar_meshes.hpp"

#include <fem/core/model.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <vector>

namespace fem::core::test {

inline constexpr double kE = 2.0e11;
inline constexpr double kNu = 0.3;
inline constexpr double kG = kE / (2.0 * (1.0 + kNu));
inline constexpr double kArea = 1.0e-2;
inline constexpr double kIy = 2.0e-5;
inline constexpr double kIz = 8.0e-5;
inline constexpr double kJ = 3.0e-5;
inline constexpr double kShearArea = 8.0e-3;

inline Material steel()
{
    return {.name = "Steel",
            .youngsModulus = kE,
            .poissonsRatio = kNu,
            .density = 7850.0,
            .thermalExpansion = 1.2e-5,
            .conductivity = {},
            .specificHeat = {}};
}

inline Section genericSection()
{
    Section section{.name = "S", .area = kArea};
    section.inertiaY = kIy;
    section.inertiaZ = kIz;
    section.torsionConstant = kJ;
    section.shearAreaY = kShearArea;
    section.shearAreaZ = kShearArea;
    return section;
}

// Every bar of the spec as a beam of `kind`, steel and the generic section.
inline Model beamModel(const TrussSpec& spec, ElementKind kind,
                       BeamTheory theory = BeamTheory::EulerBernoulli)
{
    Model model;
    model.mesh = trussMesh(spec);
    model.materials.push_back(steel());
    model.sections.push_back(genericSection());
    ElementIds all;
    for (std::size_t j = 0; j < spec.bars.size(); ++j) {
        all.ids.push_back(static_cast<ExternalId>(j) + 1);
    }
    model.assignments.push_back({.elements = all,
                                 .kind = kind,
                                 .material = "Steel",
                                 .section = "S",
                                 .theory = theory});
    return model;
}

inline void restrain(Model& model, std::initializer_list<ExternalId> nodes,
                     std::initializer_list<DofType> dofs, double value = 0.0)
{
    for (const DofType dof : dofs) {
        model.restraints.push_back(
            {.nodes = NodeIds{std::vector<ExternalId>(nodes)}, .dof = dof, .value = value});
    }
}

inline void fixAll(Model& model, std::initializer_list<ExternalId> nodes, ElementKind kind)
{
    if (kind == ElementKind::Beam2D) {
        restrain(model, nodes, {DofType::U, DofType::V, DofType::RZ});
    } else {
        restrain(model, nodes,
                 {DofType::U, DofType::V, DofType::W, DofType::RX, DofType::RY, DofType::RZ});
    }
}

inline StaticResult solve(const Model& model)
{
    auto result = runLinearStatic(model);
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    return std::move(*result);
}

inline const BeamResult& beamOf(const StaticResult& result, std::size_t element)
{
    REQUIRE(result.beams.at(element).has_value());
    return *result.beams[element];
}

// |value - reference| within `tolerance` of max(|reference|, scale).
inline bool near(double value, double reference, double tolerance, double scale = 0.0)
{
    return std::abs(value - reference) <= tolerance * std::max(std::abs(reference), scale);
}

} // namespace fem::core::test
