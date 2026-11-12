#pragma once

// Small meshes shared by the render tests.

#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace fem::render::test {

using ElementList = std::vector<std::pair<core::ElementType, std::vector<core::ExternalId>>>;

// Nodes get ids 0, 1, 2...; elements 1, 2, 3...
inline std::shared_ptr<const core::Mesh> buildMesh(const std::vector<Eigen::Vector3d>& nodes,
                                                   const ElementList& elements,
                                                   const std::vector<std::pair<std::string, std::vector<int>>>& groups = {})
{
    core::MeshBuilder builder;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        REQUIRE(builder.addNode(static_cast<core::ExternalId>(i), nodes[i]).has_value());
    }
    std::vector<core::ElementHandle> handles;
    core::ExternalId id = 1;
    for (const auto& [type, ids] : elements) {
        const auto handle = builder.addElement(type, id++, ids);
        REQUIRE(handle.has_value());
        handles.push_back(handle.value_or(core::ElementHandle{}));
    }
    for (const auto& [name, members] : groups) {
        std::vector<core::ElementHandle> list;
        for (const int m : members) {
            list.push_back(handles.at(static_cast<std::size_t>(m)));
        }
        const int dimension = core::elementTypeInfo(list.front().type).dimension;
        REQUIRE(builder.addGroup(name, dimension, list).has_value());
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    return std::make_shared<const core::Mesh>(std::move(mesh).value());
}

// n x n x n unit hexahedra from the origin; element (i, j, k) has id
// 1 + i + n * (j + n * k).
inline std::shared_ptr<const core::Mesh> hexGrid(int n, const Eigen::Vector3d& shift = Eigen::Vector3d::Zero())
{
    const int side = n + 1;
    std::vector<Eigen::Vector3d> nodes;
    for (int k = 0; k < side; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                nodes.push_back(Eigen::Vector3d(i, j, k) + shift);
            }
        }
    }
    const auto at = [side](int i, int j, int k) -> core::ExternalId {
        return i + side * (j + side * k);
    };
    ElementList elements;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                elements.emplace_back(
                    core::ElementType::Hex8,
                    std::vector<core::ExternalId>{at(i, j, k), at(i + 1, j, k), at(i + 1, j + 1, k),
                                                  at(i, j + 1, k), at(i, j, k + 1),
                                                  at(i + 1, j, k + 1), at(i + 1, j + 1, k + 1),
                                                  at(i, j + 1, k + 1)});
            }
        }
    }
    return buildMesh(nodes, elements);
}

// n x n unit quadrilaterals in the XY plane; element (i, j) has id 1 + i + n * j
// and node (i, j) has id i + (n + 1) * j.
inline std::shared_ptr<const core::Mesh> quadGrid(int n)
{
    const int side = n + 1;
    std::vector<Eigen::Vector3d> nodes;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            nodes.emplace_back(i, j, 0.0);
        }
    }
    ElementList elements;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const core::ExternalId a = i + side * j;
            elements.emplace_back(core::ElementType::Quad4,
                                  std::vector<core::ExternalId>{a, a + 1, a + 1 + side, a + side});
        }
    }
    return buildMesh(nodes, elements);
}

} // namespace fem::render::test
