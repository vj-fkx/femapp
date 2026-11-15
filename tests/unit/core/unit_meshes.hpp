#pragma once

// Structured meshes of the unit interval, square or cube for the kernel
// tests (design 006 §5.8): segments, triangles, quadrilaterals,
// tetrahedra (6 per cube), hexahedra, wedges and pyramids-free; linear or
// quadratic. Higher order nodes are placed by the linear map of each
// element, and nodes are shared by coordinates.

#include <fem/core/element_type.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/shape_functions.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

namespace fem::core::test {

struct UnitMesh
{
    Mesh mesh;
    std::vector<NodeIndex> boundary;    // nodes on the boundary of the unit domain
    std::vector<ElementIndex> elements; // all of them
    int dimension = 0;
};

inline ElementType linearOf(ElementType type)
{
    switch (elementTypeInfo(type).family) {
    case ElementFamily::Line:
        return ElementType::Line2;
    case ElementFamily::Triangle:
        return ElementType::Tri3;
    case ElementFamily::Quadrilateral:
        return ElementType::Quad4;
    case ElementFamily::Tetrahedron:
        return ElementType::Tet4;
    case ElementFamily::Hexahedron:
        return ElementType::Hex8;
    case ElementFamily::Wedge:
        return ElementType::Wedge6;
    default:
        return type;
    }
}

// Corners of the linear elements of an n^d grid, in Gmsh vertex order, with a
// positive orientation.
inline std::vector<std::vector<Eigen::Vector3d>> linearCells(ElementType linear, int n)
{
    std::vector<std::vector<Eigen::Vector3d>> cells;
    const double h = 1.0 / n;
    const auto p = [h](int i, int j, int k) { return Eigen::Vector3d(i * h, j * h, k * h); };
    switch (linear) {
    case ElementType::Line2:
        for (int i = 0; i < n; ++i) {
            cells.push_back({p(i, 0, 0), p(i + 1, 0, 0)});
        }
        break;
    case ElementType::Tri3:
    case ElementType::Quad4:
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const auto a = p(i, j, 0);
                const auto b = p(i + 1, j, 0);
                const auto c = p(i + 1, j + 1, 0);
                const auto d = p(i, j + 1, 0);
                if (linear == ElementType::Quad4) {
                    cells.push_back({a, b, c, d});
                } else {
                    cells.push_back({a, b, c});
                    cells.push_back({a, c, d});
                }
            }
        }
        break;
    case ElementType::Wedge6:
        for (int k = 0; k < n; ++k) {
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    const std::array<std::array<int, 2>, 4> square = {
                        {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
                    for (const auto& tri :
                         {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
                        std::vector<Eigen::Vector3d> cell;
                        for (int layer = 0; layer < 2; ++layer) {
                            for (const int v : tri) {
                                const auto& s = square.at(static_cast<std::size_t>(v));
                                cell.push_back(p(s[0], s[1], k + layer));
                            }
                        }
                        cells.push_back(cell);
                    }
                }
            }
        }
        break;
    case ElementType::Hex8:
    case ElementType::Tet4:
        for (int k = 0; k < n; ++k) {
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    const auto v = [&](int x, int y, int z) { return p(i + x, j + y, k + z); };
                    if (linear == ElementType::Hex8) {
                        cells.push_back({v(0, 0, 0), v(1, 0, 0), v(1, 1, 0), v(0, 1, 0), v(0, 0, 1),
                                         v(1, 0, 1), v(1, 1, 1), v(0, 1, 1)});
                        continue;
                    }
                    // Kuhn: one tetrahedron per order of the axes.
                    std::array<int, 3> axes = {0, 1, 2};
                    do {
                        std::array<int, 3> at = {0, 0, 0};
                        std::vector<Eigen::Vector3d> cell = {v(0, 0, 0)};
                        for (const int axis : axes) {
                            at.at(static_cast<std::size_t>(axis)) = 1;
                            cell.push_back(v(at[0], at[1], at[2]));
                        }
                        const double volume =
                            (cell[1] - cell[0]).cross(cell[2] - cell[0]).dot(cell[3] - cell[0]);
                        if (volume < 0.0) {
                            std::swap(cell[1], cell[2]);
                        }
                        cells.push_back(cell);
                    } while (std::ranges::next_permutation(axes).found);
                }
            }
        }
        break;
    default:
        break;
    }
    return cells;
}

// A structured mesh of the unit domain with elements of `type`; with
// `distortion` > 0 the nodes are moved by a smooth map that keeps the
// boundary in place.
inline UnitMesh unitMesh(ElementType type, int n, double distortion = 0.0)
{
    const auto& info = elementTypeInfo(type);
    const ElementType linear = linearOf(type);
    const auto reference = referenceNodeCoordinates(type);
    const int dims = info.dimension;

    // Nodes keyed by their position on the 2n grid.
    std::map<std::array<long long, 3>, std::pair<ExternalId, Eigen::Vector3d>> nodes;
    std::vector<std::vector<ExternalId>> elements;
    ShapeValues values;
    for (const auto& cell : linearCells(linear, n)) {
        std::vector<ExternalId> ids;
        for (const auto& xi : reference) {
            evaluateShape(linear, xi, values);
            Eigen::Vector3d x = Eigen::Vector3d::Zero();
            for (std::size_t c = 0; c < cell.size(); ++c) {
                x += values.n.at(c) * cell[c];
            }
            const std::array<long long, 3> key = {std::llround(x.x() * 2 * n),
                                                  std::llround(x.y() * 2 * n),
                                                  std::llround(x.z() * 2 * n)};
            auto [it, inserted] =
                nodes.try_emplace(key, static_cast<ExternalId>(nodes.size() + 1), x);
            ids.push_back(it->second.first);
        }
        elements.push_back(ids);
    }

    // Boundary nodes and the distortion.
    std::vector<std::pair<ExternalId, Eigen::Vector3d>> ordered(nodes.size());
    std::vector<ExternalId> boundaryIds;
    for (auto& [key, entry] : nodes) {
        Eigen::Vector3d x = entry.second;
        bool onBoundary = false;
        for (int k = 0; k < dims; ++k) {
            onBoundary = onBoundary || std::abs(x(k)) < 1e-12 || std::abs(x(k) - 1.0) < 1e-12;
        }
        if (onBoundary) {
            boundaryIds.push_back(entry.first);
        }
        if (distortion > 0.0) {
            const Eigen::Vector3d original = x;
            for (int k = 0; k < dims; ++k) {
                double shift = distortion * std::sin(2.0 * std::numbers::pi * original(k));
                for (int m = 0; m < dims; ++m) {
                    if (m != k) {
                        shift *= std::sin(std::numbers::pi * original(m));
                    }
                }
                x(k) += shift;
            }
        }
        ordered[static_cast<std::size_t>(entry.first - 1)] = {entry.first, x};
    }

    MeshBuilder builder;
    for (const auto& [id, x] : ordered) {
        REQUIRE(builder.addNode(id, x).has_value());
    }
    ExternalId elementId = 1;
    for (const auto& ids : elements) {
        REQUIRE(builder.addElement(type, elementId++, ids).has_value());
    }
    auto built = builder.build();
    REQUIRE(built.has_value());

    UnitMesh result{.mesh = std::move(*built), .boundary = {}, .elements = {}, .dimension = dims};
    for (const ExternalId id : boundaryIds) {
        result.boundary.push_back(result.mesh.findNode(id).value_or(0));
    }
    result.elements.resize(static_cast<std::size_t>(result.mesh.elementCount()));
    std::iota(result.elements.begin(), result.elements.end(), ElementIndex{0});
    return result;
}

} // namespace fem::core::test
