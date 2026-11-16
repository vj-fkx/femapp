#pragma once

// Small bar meshes for the truss tests (feature 007). Node i (0-based) gets
// external id i + 1 and bar j gets id j + 1; a point group adds one Point1
// element per node, with ids from kPointIdBase.

#include <fem/core/mesh.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fem::core::test {

inline constexpr ExternalId kPointIdBase = 100000;

struct TrussSpec
{
    std::vector<Eigen::Vector3d> nodes;
    std::vector<std::array<int, 2>> bars; // 0-based node indices
    // Name and 0-based bar indices.
    std::vector<std::pair<std::string, std::vector<int>>> lineGroups;
    // Name and 0-based node indices.
    std::vector<std::pair<std::string, std::vector<int>>> pointGroups;
};

inline std::shared_ptr<const Mesh> trussMesh(const TrussSpec& spec)
{
    MeshBuilder builder;
    for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
        REQUIRE(builder.addNode(static_cast<ExternalId>(i) + 1, spec.nodes[i]).has_value());
    }
    std::vector<ElementHandle> bars;
    for (std::size_t j = 0; j < spec.bars.size(); ++j) {
        const std::array<ExternalId, 2> ids = {ExternalId{spec.bars[j][0]} + 1,
                                               ExternalId{spec.bars[j][1]} + 1};
        auto handle = builder.addElement(ElementType::Line2, static_cast<ExternalId>(j) + 1, ids);
        REQUIRE(handle.has_value());
        bars.push_back(handle.value_or(ElementHandle{}));
    }
    ExternalId pointId = kPointIdBase;
    std::vector<std::pair<std::string, std::vector<ElementHandle>>> pointHandles;
    for (const auto& [name, nodes] : spec.pointGroups) {
        std::vector<ElementHandle> handles;
        for (const int node : nodes) {
            const std::array<ExternalId, 1> ids = {ExternalId{node} + 1};
            auto handle = builder.addElement(ElementType::Point1, pointId++, ids);
            REQUIRE(handle.has_value());
            handles.push_back(handle.value_or(ElementHandle{}));
        }
        pointHandles.emplace_back(name, std::move(handles));
    }
    for (const auto& [name, indices] : spec.lineGroups) {
        std::vector<ElementHandle> handles;
        for (const int j : indices) {
            handles.push_back(bars.at(static_cast<std::size_t>(j)));
        }
        REQUIRE(builder.addGroup(name, 1, std::move(handles)).has_value());
    }
    for (auto& [name, handles] : pointHandles) {
        REQUIRE(builder.addGroup(name, 0, std::move(handles)).has_value());
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    return std::make_shared<const Mesh>(std::move(*mesh));
}

} // namespace fem::core::test
