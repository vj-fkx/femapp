#include <fem/core/mesh_surface.hpp>
#include <fem/render/mesh_picker.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace fem::render {

namespace {

using Vec3f = Eigen::Vector3f;

constexpr double kInfinity = std::numeric_limits<double>::infinity();

struct ScreenPoint
{
    Vec2 pixel = Vec2::Zero();
    double depth = 0.0; // distance in front of the camera along its viewing direction
    bool inFront = false;
};

// Projects scene-relative positions to pixels for one camera.
class Projector
{
public:
    Projector(const Camera& camera, const Eigen::Vector3d& origin)
        : m_width(camera.viewportWidth())
        , m_height(camera.viewportHeight())
    {
        Mat4 translation = Mat4::Identity();
        translation.block<3, 1>(0, 3) = origin;
        m_view = camera.viewMatrix() * translation;
        m_clip = camera.projectionMatrix() * m_view;
    }

    [[nodiscard]] ScreenPoint project(const Vec3f& relative) const
    {
        const Eigen::Vector4d p(relative.x(), relative.y(), relative.z(), 1.0);
        const Eigen::Vector4d clip = m_clip * p;
        ScreenPoint result;
        result.depth = -(m_view.row(2).dot(p));
        result.inFront = clip.w() > 0.0;
        if (result.inFront) {
            const double x = clip.x() / clip.w();
            const double y = clip.y() / clip.w();
            result.pixel = {(x + 1.0) * 0.5 * m_width, (1.0 - y) * 0.5 * m_height};
        }
        return result;
    }

private:
    Mat4 m_view;
    Mat4 m_clip;
    double m_width;
    double m_height;
};

// Ray in scene-relative coordinates.
struct LocalRay
{
    Vec3f origin;
    Vec3f direction;
};

LocalRay localRay(const Camera& camera, const Vec2& pixel, const Eigen::Vector3d& origin)
{
    const Ray ray = camera.rayThrough(pixel);
    return {.origin = (ray.origin - origin).cast<float>(),
            .direction = ray.direction.cast<float>()};
}

// Möller–Trumbore; returns the ray parameter of the hit, or a negative value.
float intersect(const LocalRay& ray, const Vec3f& a, const Vec3f& b, const Vec3f& c)
{
    constexpr float kEpsilon = 1e-12F;
    const Vec3f ab = b - a;
    const Vec3f ac = c - a;
    const Vec3f p = ray.direction.cross(ac);
    const float det = ab.dot(p);
    if (std::abs(det) < kEpsilon) {
        return -1.0F;
    }
    const float inverse = 1.0F / det;
    const Vec3f s = ray.origin - a;
    const float u = s.dot(p) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }
    const Vec3f q = s.cross(ab);
    const float v = ray.direction.dot(q) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }
    return ac.dot(q) * inverse;
}

struct TriangleHit
{
    double depth = kInfinity;
    core::ElementIndex element = -1;
};

// Closest triangle of the visible dimensions along a ray.
TriangleHit closestTriangle(const MeshScene& scene, const LocalRay& ray, const Vec3f& eye,
                            const Vec3f& forward, const DisplayOptions& display)
{
    TriangleHit best;
    const auto vertices = scene.triangleVertices();
    const auto elements = scene.triangleElements();
    for (const int dimension : {2, 3}) {
        if (!display.isVisible(dimension)) {
            continue;
        }
        const Range range = scene.triangles(dimension);
        for (std::uint32_t t = range.first; t < range.first + range.count; ++t) {
            const std::size_t v = static_cast<std::size_t>(t) * 3;
            const float hit = intersect(ray, vertices[v], vertices[v + 1], vertices[v + 2]);
            if (hit < 0.0F) {
                continue;
            }
            const Vec3f point = ray.origin + hit * ray.direction;
            const double depth = (point - eye).dot(forward);
            if (depth > 0.0 && depth < best.depth) {
                best = {.depth = depth, .element = elements[t]};
            }
        }
    }
    return best;
}

// Distance from p to the segment ab, and the parameter of the closest point.
std::pair<double, double> distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const Vec2 ab = b - a;
    const double length2 = ab.squaredNorm();
    const double u = length2 > 0.0 ? std::clamp((p - a).dot(ab) / length2, 0.0, 1.0) : 0.0;
    return {(p - (a + u * ab)).norm(), u};
}

struct Box2
{
    Vec2 min;
    Vec2 max;

    [[nodiscard]] bool contains(const Vec2& p) const noexcept
    {
        return p.x() >= min.x() && p.x() <= max.x() && p.y() >= min.y() && p.y() <= max.y();
    }
};

// Liang–Barsky: does the segment ab touch the box?
bool segmentTouchesBox(const Vec2& a, const Vec2& b, const Box2& box)
{
    double t0 = 0.0;
    double t1 = 1.0;
    const Vec2 d = b - a;
    const std::array<std::pair<double, double>, 4> planes = {{
        {-d.x(), a.x() - box.min.x()},
        {d.x(), box.max.x() - a.x()},
        {-d.y(), a.y() - box.min.y()},
        {d.y(), box.max.y() - a.y()},
    }};
    for (const auto& [p, q] : planes) {
        if (p == 0.0) {
            if (q < 0.0) {
                return false;
            }
            continue;
        }
        const double r = q / p;
        if (p < 0.0) {
            t0 = std::max(t0, r);
        } else {
            t1 = std::min(t1, r);
        }
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c)
{
    const auto cross = [](const Vec2& u, const Vec2& v) { return u.x() * v.y() - u.y() * v.x(); };
    const double d1 = cross(b - a, p - a);
    const double d2 = cross(c - b, p - b);
    const double d3 = cross(a - c, p - c);
    const bool negative = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
    const bool positive = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
    return !(negative && positive);
}

// Edges and faces (as vertex index lists) of an element, for crossing tests.
struct Outline
{
    std::vector<std::pair<int, int>> edges;
    std::vector<std::array<int, 3>> triangles;
};

Outline outlineOf(const core::ElementTypeInfo& info)
{
    Outline outline;
    const auto addPolygon = [&](std::span<const int> polygon) {
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            outline.edges.emplace_back(polygon[i], polygon[(i + 1) % polygon.size()]);
        }
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            outline.triangles.push_back({polygon[0], polygon[i], polygon[i + 1]});
        }
    };
    if (info.dimension == 1) {
        outline.edges.emplace_back(0, 1);
    } else if (info.dimension == 2) {
        const std::array<int, 4> polygon = {0, 1, 2, 3};
        addPolygon(
            std::span<const int>(polygon.data(), static_cast<std::size_t>(info.vertexCount)));
    } else if (info.dimension == 3) {
        for (const core::LocalFace& face : core::elementFaces(info.family)) {
            std::array<int, 4> polygon{};
            for (std::size_t v = 0; v < face.vertexCount; ++v) {
                polygon.at(v) = face.vertices.at(v);
            }
            addPolygon(std::span<const int>(polygon.data(), face.vertexCount));
        }
    }
    return outline;
}

} // namespace

MeshPicker::MeshPicker(std::shared_ptr<const MeshScene> scene)
    : m_scene(std::move(scene))
{}

std::optional<int> MeshPicker::pick(const Camera& camera, const Vec2& pixel, SelectionMode mode,
                                    const DisplayOptions& display, double tolerancePx) const
{
    const MeshScene& scene = *m_scene;
    const Projector projector(camera, scene.origin());
    const Vec3f eye = (camera.position() - scene.origin()).cast<float>();
    const Vec3f forward = camera.forward().cast<float>();
    const LocalRay ray = localRay(camera, pixel, scene.origin());
    const auto positions = scene.nodePositions();
    const auto at = [&](core::NodeIndex node) { return positions[static_cast<std::size_t>(node)]; };

    if (mode == SelectionMode::Elements) {
        TriangleHit best = closestTriangle(scene, ray, eye, forward, display);

        if (display.isVisible(1)) {
            const Range range = scene.lines(LineSet::Elements1d);
            const auto vertices = scene.lineVertices();
            for (std::uint32_t s = range.first; s < range.first + range.count; ++s) {
                const ScreenPoint a = projector.project(vertices[2 * static_cast<std::size_t>(s)]);
                const ScreenPoint b =
                    projector.project(vertices[2 * static_cast<std::size_t>(s) + 1]);
                if (!a.inFront || !b.inFront) {
                    continue;
                }
                const auto [distance, u] = distanceToSegment(pixel, a.pixel, b.pixel);
                const double depth = a.depth + u * (b.depth - a.depth);
                if (distance <= tolerancePx && depth < best.depth) {
                    best = {.depth = depth, .element = scene.lineElements()[s]};
                }
            }
        }
        if (display.isVisible(0)) {
            for (const core::ElementIndex element : scene.pointElements()) {
                const ScreenPoint p = projector.project(at(scene.mesh().elementNodes(element)[0]));
                if (p.inFront && (p.pixel - pixel).norm() <= tolerancePx && p.depth < best.depth) {
                    best = {.depth = p.depth, .element = element};
                }
            }
        }
        if (best.element < 0) {
            return std::nullopt;
        }
        return best.element;
    }

    // Nodes: candidates within the tolerance, closest first; the first one not
    // hidden behind a face wins.
    std::vector<std::pair<double, core::NodeIndex>> candidates;
    for (int dimension = 0; dimension < 4; ++dimension) {
        if (!display.isVisible(dimension)) {
            continue;
        }
        for (const core::NodeIndex node : scene.surfaceNodes(dimension)) {
            const ScreenPoint p = projector.project(at(node));
            if (p.inFront && (p.pixel - pixel).norm() <= tolerancePx) {
                candidates.emplace_back(p.depth, node);
            }
        }
    }
    std::ranges::sort(candidates);
    const auto& box = scene.bounds();
    const double epsilon =
        box.isEmpty() ? 1e-9 : 1e-4 * (box.max - box.min).norm() + 1e-6 * camera.distance();
    for (const auto& [depth, node] : candidates) {
        const ScreenPoint p = projector.project(at(node));
        const LocalRay toNode = localRay(camera, p.pixel, scene.origin());
        const TriangleHit hit = closestTriangle(scene, toNode, eye, forward, display);
        if (depth <= hit.depth + epsilon) {
            return node;
        }
    }
    return std::nullopt;
}

std::vector<int> MeshPicker::pickRect(const Camera& camera, const PickRect& rect,
                                      SelectionMode mode, const DisplayOptions& display) const
{
    const MeshScene& scene = *m_scene;
    const core::Mesh& mesh = scene.mesh();
    const Projector projector(camera, scene.origin());
    const Box2 box{.min = rect.from.cwiseMin(rect.to), .max = rect.from.cwiseMax(rect.to)};
    const bool crossing = rect.isCrossing();

    std::vector<ScreenPoint> screen(static_cast<std::size_t>(mesh.nodeCount()));
    const auto positions = scene.nodePositions();
    for (std::size_t n = 0; n < screen.size(); ++n) {
        screen[n] = projector.project(positions[n]);
    }
    const auto onScreen = [&](core::NodeIndex node) -> const ScreenPoint& {
        return screen[static_cast<std::size_t>(node)];
    };

    std::vector<int> result;
    if (mode == SelectionMode::Nodes) {
        for (int dimension = 0; dimension < 4; ++dimension) {
            if (!display.isVisible(dimension)) {
                continue;
            }
            for (const core::NodeIndex node : scene.allNodes(dimension)) {
                const ScreenPoint& p = onScreen(node);
                if (p.inFront && box.contains(p.pixel)) {
                    result.push_back(node);
                }
            }
        }
        std::ranges::sort(result);
        const auto duplicates = std::ranges::unique(result);
        result.erase(duplicates.begin(), duplicates.end());
        return result;
    }

    const Vec2 center = 0.5 * (box.min + box.max);
    for (const core::ElementBlock& block : mesh.blocks()) {
        const core::ElementTypeInfo& info = core::elementTypeInfo(block.type);
        if (!display.isVisible(info.dimension)) {
            continue;
        }
        const Outline outline = outlineOf(info);
        for (int i = 0; i < block.elementCount(); ++i) {
            const core::ElementIndex element = block.firstElement + i;
            const auto nodes = mesh.elementNodes(element);
            const auto vertex = [&](int v) -> const ScreenPoint& {
                return onScreen(nodes[static_cast<std::size_t>(v)]);
            };

            bool allInside = true;
            bool anyInside = false;
            bool allInFront = true;
            for (int v = 0; v < info.vertexCount; ++v) {
                const ScreenPoint& p = vertex(v);
                const bool inside = p.inFront && box.contains(p.pixel);
                allInside = allInside && inside;
                anyInside = anyInside || inside;
                allInFront = allInFront && p.inFront;
            }

            bool selected = allInside;
            if (!selected && crossing && allInFront) {
                selected = anyInside;
                for (std::size_t e = 0; !selected && e < outline.edges.size(); ++e) {
                    const auto [a, b] = outline.edges[e];
                    selected = segmentTouchesBox(vertex(a).pixel, vertex(b).pixel, box);
                }
                for (std::size_t t = 0; !selected && t < outline.triangles.size(); ++t) {
                    const auto& tri = outline.triangles[t];
                    selected = pointInTriangle(center, vertex(tri[0]).pixel, vertex(tri[1]).pixel,
                                               vertex(tri[2]).pixel);
                }
            }
            if (selected) {
                result.push_back(element);
            }
        }
    }
    return result;
}

} // namespace fem::render
