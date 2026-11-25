#pragma once

// The space frame building of bench_frame (NFR-008-02), shared with
// bench_run_control (NFR-011-03): (bays + 1)^2 columns and `storeys`
// storeys, columns between the levels, beams along x and y at each level,
// the base fixed, self-weight and a wind load at the top. With `warping`,
// every bar has torsion with warping and the columns are open I sections.

#include <fem/core/mesh_builder.hpp>
#include <fem/core/model.hpp>
#include <fem/core/section.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fem::perf {

using Clock = std::chrono::steady_clock;

// The index of an id the model is known to have.
template <typename T>
inline T found(const std::optional<T>& index)
{
    if (!index) {
        throw std::runtime_error("an id of the frame is missing");
    }
    return *index;
}

inline double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

inline fem::core::Model building(int bays, int storeys, bool warping)
{
    const double bay = 6.0;
    const double storey = 3.5;
    const int side = bays + 1;
    const auto id = [side](int i, int j, int k) {
        return 1 + i + fem::core::ExternalId{side} * (j + fem::core::ExternalId{side} * k);
    };
    fem::core::MeshBuilder builder;
    for (int k = 0; k <= storeys; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                (void)builder.addNode(id(i, j, k), Eigen::Vector3d(i * bay, j * bay, k * storey));
            }
        }
    }
    fem::core::ExternalId element = 1;
    std::vector<fem::core::ExternalId> columns;
    std::vector<fem::core::ExternalId> beams;
    const auto add = [&](fem::core::ExternalId a, fem::core::ExternalId b,
                         std::vector<fem::core::ExternalId>& list) {
        const std::array<fem::core::ExternalId, 2> nodes = {a, b};
        (void)builder.addElement(fem::core::ElementType::Line2, element, nodes);
        list.push_back(element++);
    };
    for (int k = 1; k <= storeys; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                add(id(i, j, k - 1), id(i, j, k), columns);
                if (i + 1 < side) {
                    add(id(i, j, k), id(i + 1, j, k), beams);
                }
                if (j + 1 < side) {
                    add(id(i, j, k), id(i, j + 1, k), beams);
                }
            }
        }
    }
    auto mesh = builder.build();
    if (!mesh) {
        throw std::runtime_error(mesh.error().message);
    }

    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = 2.1e11,
                               .poissonsRatio = 0.3,
                               .density = 7850.0,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    if (warping) {
        model.sections.push_back(
            {.name = "Column",
             .shape = fem::core::ISection{.h = 0.4, .bf = 0.3, .tf = 0.024, .tw = 0.0135}});
    } else {
        model.sections.push_back(
            {.name = "Column", .shape = fem::core::RectangularTube{.b = 0.4, .h = 0.4, .t = 0.02}});
    }
    model.sections.push_back(
        {.name = "Beam",
         .shape = fem::core::ISection{.h = 0.5, .bf = 0.2, .tf = 0.016, .tw = 0.01}});
    model.assignments.push_back({.elements = fem::core::ElementIds{columns},
                                 .kind = fem::core::ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "Column",
                                 .warping = warping});
    model.assignments.push_back({.elements = fem::core::ElementIds{beams},
                                 .kind = fem::core::ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "Beam",
                                 .warping = warping});
    fem::core::NodeIds base;
    fem::core::NodeIds top;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            base.ids.push_back(id(i, j, 0));
            top.ids.push_back(id(i, j, storeys));
        }
    }
    for (const auto dof :
         {fem::core::DofType::U, fem::core::DofType::V, fem::core::DofType::W,
          fem::core::DofType::RX, fem::core::DofType::RY, fem::core::DofType::RZ}) {
        model.restraints.push_back({.nodes = base, .dof = dof, .value = 0.0});
    }
    model.nodalForces.push_back({.nodes = top, .force = Eigen::Vector3d(2.0e4, 1.0e4, 0.0)});
    model.gravity = Eigen::Vector3d(0.0, 0.0, -9.81);
    return model;
}

} // namespace fem::perf
