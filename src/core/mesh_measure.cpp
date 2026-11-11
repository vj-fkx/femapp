#include <fem/core/mesh_measure.hpp>

#include <Eigen/Geometry>
#include <array>
#include <cstddef>
#include <span>

namespace fem::core {

namespace {

using Vec3 = Eigen::Vector3d;

double tetVolume(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    return (b - a).dot((c - a).cross(d - a)) / 6.0;
}

template <std::size_t N>
double sumTets(std::span<const Vec3> v, const std::array<std::array<int, 4>, N>& tets)
{
    double volume = 0.0;
    for (const auto& t : tets) {
        volume += tetVolume(v[static_cast<std::size_t>(t[0])], v[static_cast<std::size_t>(t[1])],
                            v[static_cast<std::size_t>(t[2])], v[static_cast<std::size_t>(t[3])]);
    }
    return volume;
}

// Hexahedron: six tetrahedra sharing the diagonal 0-6.
constexpr std::array<std::array<int, 4>, 6> kHexTets = {{
    {0, 1, 2, 6},
    {0, 2, 3, 6},
    {0, 3, 7, 6},
    {0, 7, 4, 6},
    {0, 4, 5, 6},
    {0, 5, 1, 6},
}};
constexpr std::array<std::array<int, 4>, 3> kWedgeTets = {{
    {0, 1, 2, 3},
    {1, 2, 3, 4},
    {2, 3, 4, 5},
}};
constexpr std::array<std::array<int, 4>, 2> kPyramidTets = {{
    {0, 1, 2, 4},
    {0, 2, 3, 4},
}};

} // namespace

double elementMeasure(const Mesh& mesh, ElementIndex element)
{
    const ElementTypeInfo& info = elementTypeInfo(mesh.elementType(element));
    const auto nodes = mesh.elementNodes(element);

    std::array<Vec3, 8> vertices;
    for (int i = 0; i < info.vertexCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        vertices.at(index) = mesh.nodeCoordinates(nodes[index]);
    }
    const std::span<const Vec3> v(vertices.data(), static_cast<std::size_t>(info.vertexCount));

    switch (info.family) {
    case ElementFamily::Point:
        return 1.0;
    case ElementFamily::Line:
        return (v[1] - v[0]).norm();
    case ElementFamily::Triangle:
    case ElementFamily::Quadrilateral: {
        const Vec3 area = info.family == ElementFamily::Triangle
                              ? 0.5 * (v[1] - v[0]).cross(v[2] - v[0])
                              : 0.5 * (v[2] - v[0]).cross(v[3] - v[1]);
        return mesh.liesInXYPlane() ? area.z() : area.norm();
    }
    case ElementFamily::Tetrahedron:
        return tetVolume(v[0], v[1], v[2], v[3]);
    case ElementFamily::Hexahedron:
        return sumTets(v, kHexTets);
    case ElementFamily::Wedge:
        return sumTets(v, kWedgeTets);
    case ElementFamily::Pyramid:
        return sumTets(v, kPyramidTets);
    }
    return 0.0;
}

} // namespace fem::core
