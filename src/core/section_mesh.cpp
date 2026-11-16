// REQ-008b-02, 04: triangulation of solid polygon sections (design 008b
// §5.1.3, ADR 0005). Ear clipping with bridged holes (Eberly, Triangulation
// by Ear Clipping, 2002), Lawson flips to the constrained Delaunay
// triangulation, Rivara's longest-edge propagation path bisection (Rivara,
// 1997), which keeps the smallest angle above half of the initial one.

#include <fem/core/detail/section_mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fem::core::detail {

namespace {

using Vec = Eigen::Vector2d;
using Ring = std::vector<Vec>;

double cross(const Vec& a, const Vec& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

double orient(const Vec& a, const Vec& b, const Vec& c)
{
    return cross(b - a, c - a);
}

double signedArea(const Ring& ring)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        twice += cross(ring[i], ring[(i + 1) % ring.size()]);
    }
    return twice / 2.0;
}

// Strictly inside (by the crossing number; points on the boundary are
// reported by the segment tests instead).
bool inside(const Ring& ring, const Vec& p)
{
    bool in = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Vec& a = ring[i];
        const Vec& b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y()) &&
            p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
            in = !in;
        }
    }
    return in;
}

// Whether the closed segments ab and cd share a point, within `tol` of
// the scale of the section.
bool segmentsMeet(const Vec& a, const Vec& b, const Vec& c, const Vec& d, double tol)
{
    const double d1 = orient(c, d, a);
    const double d2 = orient(c, d, b);
    const double d3 = orient(a, b, c);
    const double d4 = orient(a, b, d);
    const double eps = tol * tol;
    if (((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) &&
        ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps))) {
        return true;
    }
    const auto onSegment = [tol](const Vec& p, const Vec& q, const Vec& r) {
        // r on pq
        const Vec pq = q - p;
        const double length = pq.norm();
        if (length == 0.0) {
            return (r - p).norm() <= tol;
        }
        const double distance = std::abs(cross(pq, r - p)) / length;
        const double along = (r - p).dot(pq) / length;
        return distance <= tol && along >= -tol && along <= length + tol;
    };
    return onSegment(c, d, a) || onSegment(c, d, b) || onSegment(a, b, c) || onSegment(a, b, d);
}

class Checker
{
public:
    Checker(const SolidPolygon& shape, std::string_view section)
        : m_shape(shape)
        , m_prefix("Section \"" + std::string(section) + "\": ")
    {}

    std::optional<std::string> check()
    {
        const auto& regions = m_shape.regions;
        if (regions.empty()) {
            return m_prefix + "a polygon section needs at least one region.";
        }
        if (!(m_shape.meshSize >= 0.0) || !std::isfinite(m_shape.meshSize)) {
            return m_prefix + "the mesh size must be positive, or 0 for the default.";
        }
        double extent = 0.0;
        for (const auto& region : regions) {
            for (const Vec& p : region.outline) {
                extent = std::max(extent, p.cwiseAbs().maxCoeff());
            }
        }
        m_tol = 1e-10 * std::max(extent, 1e-300);
        for (std::size_t r = 0; r < regions.size(); ++r) {
            const auto& region = regions[r];
            if (auto problem = ring(region.outline, outlineName(r))) {
                return problem;
            }
            for (std::size_t h = 0; h < region.holes.size(); ++h) {
                if (auto problem = ring(region.holes[h], holeName(r, h))) {
                    return problem;
                }
                const Ring& hole = region.holes[h];
                if (crosses(hole, region.outline) || !inside(region.outline, hole.front())) {
                    return m_prefix + "the " + holeName(r, h) + " is not inside its outline.";
                }
                for (std::size_t g = 0; g < h; ++g) {
                    const Ring& other = region.holes[g];
                    if (crosses(hole, other) || inside(other, hole.front()) ||
                        inside(hole, other.front())) {
                        return m_prefix + "the holes " + std::to_string(g) + " and " +
                               std::to_string(h) + " of the region " + std::to_string(r) +
                               " overlap.";
                    }
                }
            }
        }
        for (std::size_t r = 0; r < regions.size(); ++r) {
            for (std::size_t s = 0; s < r; ++s) {
                if (overlap(regions[s], regions[r])) {
                    return m_prefix + "the regions " + std::to_string(s) + " and " +
                           std::to_string(r) + " overlap.";
                }
            }
        }
        return std::nullopt;
    }

private:
    static std::string outlineName(std::size_t r)
    {
        return "outline of the region " + std::to_string(r);
    }
    static std::string holeName(std::size_t r, std::size_t h)
    {
        return "hole " + std::to_string(h) + " of the region " + std::to_string(r);
    }

    [[nodiscard]] std::optional<std::string> ring(const Ring& points, const std::string& name) const
    {
        if (points.size() < 3) {
            return m_prefix + "the " + name + " has fewer than three vertices.";
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!points[i].allFinite()) {
                return m_prefix + "the vertex " + std::to_string(i) + " of the " + name +
                       " is not finite.";
            }
        }
        const std::size_t n = points.size();
        for (std::size_t i = 0; i < n; ++i) {
            if ((points[(i + 1) % n] - points[i]).norm() <= m_tol) {
                return m_prefix + "the " + name + " repeats the vertex " + std::to_string(i) + ".";
            }
        }
        if (std::abs(signedArea(points)) <= m_tol * m_tol) {
            return m_prefix + "the " + name + " has zero area.";
        }
        for (std::size_t i = 0; i < n; ++i) {
            const Vec& a = points[i];
            const Vec& b = points[(i + 1) % n];
            for (std::size_t j = i + 1; j < n; ++j) {
                const bool adjacent = j == i + 1 || (i == 0 && j == n - 1);
                const Vec& c = points[j];
                const Vec& d = points[(j + 1) % n];
                if (adjacent) {
                    // Adjacent edges only share their common vertex: no backtrack.
                    const Vec& shared = j == i + 1 ? b : a;
                    const Vec& p = j == i + 1 ? a : b;
                    const Vec& q = j == i + 1 ? d : c;
                    const Vec u = p - shared;
                    const Vec v = q - shared;
                    if (std::abs(cross(u, v)) <= m_tol * (u.norm() + v.norm()) && u.dot(v) > 0.0) {
                        return m_prefix + "the " + name + " crosses itself at the vertex " +
                               std::to_string(j == i + 1 ? j : i) + ".";
                    }
                    continue;
                }
                if (segmentsMeet(a, b, c, d, m_tol)) {
                    return m_prefix + "the " + name + " crosses itself at the vertex " +
                           std::to_string(j) + ".";
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool crosses(const Ring& p, const Ring& q) const
    {
        for (std::size_t i = 0; i < p.size(); ++i) {
            for (std::size_t j = 0; j < q.size(); ++j) {
                if (segmentsMeet(p[i], p[(i + 1) % p.size()], q[j], q[(j + 1) % q.size()], m_tol)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Material of a region: inside the outline and not inside a hole.
    static bool inMaterial(const SolidPolygon::Region& region, const Vec& p)
    {
        if (!inside(region.outline, p)) {
            return false;
        }
        return std::ranges::none_of(region.holes,
                                    [&p](const Ring& hole) { return inside(hole, p); });
    }

    [[nodiscard]] bool overlap(const SolidPolygon::Region& a, const SolidPolygon::Region& b) const
    {
        const auto rings = [](const SolidPolygon::Region& region) {
            std::vector<const Ring*> all = {&region.outline};
            for (const Ring& hole : region.holes) {
                all.push_back(&hole);
            }
            return all;
        };
        for (const Ring* p : rings(a)) {
            for (const Ring* q : rings(b)) {
                if (crosses(*p, *q)) {
                    return true;
                }
            }
        }
        return inMaterial(a, b.outline.front()) || inMaterial(b, a.outline.front());
    }

    const SolidPolygon& m_shape;
    std::string m_prefix;
    double m_tol = 0.0;
};

std::uint64_t edgeKey(int a, int b)
{
    const auto lo = static_cast<std::uint64_t>(std::min(a, b));
    const auto hi = static_cast<std::uint64_t>(std::max(a, b));
    return (lo << 32U) | hi;
}

// The triangulation being built: counterclockwise triangles of node
// indices, with the triangles on each edge.
class Triangulation
{
public:
    explicit Triangulation(double scale)
        : m_scale(scale)
    {}

    int addNode(const Vec& p)
    {
        m_nodes.push_back(p);
        return static_cast<int>(m_nodes.size()) - 1;
    }
    [[nodiscard]] const Vec& node(int i) const { return m_nodes[static_cast<std::size_t>(i)]; }

    void constrain(int a, int b) { m_constrained.insert(edgeKey(a, b)); }

    int addTriangle(int a, int b, int c)
    {
        m_triangles.push_back({a, b, c});
        m_alive.push_back(true);
        const int t = static_cast<int>(m_triangles.size()) - 1;
        for (int k = 0; k < 3; ++k) {
            auto& owners = m_edges
                               .try_emplace(edgeKey(vertex(t, k), vertex(t, k + 1)),
                                            std::array<int, 2>{-1, -1})
                               .first->second;
            (owners[0] < 0 ? owners[0] : owners[1]) = t;
        }
        return t;
    }

    void removeTriangle(int t)
    {
        m_alive[static_cast<std::size_t>(t)] = false;
        for (int k = 0; k < 3; ++k) {
            const auto key = edgeKey(vertex(t, k), vertex(t, k + 1));
            auto& owners = m_edges[key];
            if (owners[0] == t) {
                owners[0] = owners[1];
            }
            owners[1] = -1;
            if (owners[0] < 0) {
                m_edges.erase(key);
            }
        }
    }

    [[nodiscard]] int vertex(int t, int k) const
    {
        return m_triangles[static_cast<std::size_t>(t)]
                          [static_cast<std::size_t>(((k % 3) + 3) % 3)];
    }
    [[nodiscard]] bool alive(int t) const { return m_alive[static_cast<std::size_t>(t)]; }
    [[nodiscard]] int triangleCount() const { return static_cast<int>(m_triangles.size()); }

    // The other triangle on the edge ab, or -1.
    [[nodiscard]] int across(int t, int a, int b) const
    {
        const auto found = m_edges.find(edgeKey(a, b));
        if (found == m_edges.end()) {
            return -1;
        }
        return found->second[0] == t ? found->second[1] : found->second[0];
    }

    // Lawson flips of the unconstrained edges until every one is locally
    // Delaunay.
    void delaunay()
    {
        std::vector<std::pair<int, int>> stack;
        for (int t = 0; t < triangleCount(); ++t) {
            if (alive(t)) {
                for (int k = 0; k < 3; ++k) {
                    stack.emplace_back(vertex(t, k), vertex(t, k + 1));
                }
            }
        }
        const double tolerance = 1e-12 * std::pow(m_scale, 4);
        while (!stack.empty()) {
            const auto [a, b] = stack.back();
            stack.pop_back();
            if (m_constrained.contains(edgeKey(a, b))) {
                continue;
            }
            const auto found = m_edges.find(edgeKey(a, b));
            if (found == m_edges.end() || found->second[1] < 0) {
                continue;
            }
            // t1 runs a -> b, t2 runs b -> a.
            int t1 = found->second[0];
            int t2 = found->second[1];
            if (!runs(t1, a, b)) {
                std::swap(t1, t2);
            }
            const int c = opposite(t1, a, b);
            const int d = opposite(t2, a, b);
            if (inCircle(node(a), node(b), node(c), node(d)) <= tolerance) {
                continue;
            }
            removeTriangle(t1);
            removeTriangle(t2);
            addTriangle(a, d, c);
            addTriangle(d, b, c);
            stack.emplace_back(a, d);
            stack.emplace_back(d, b);
            stack.emplace_back(b, c);
            stack.emplace_back(c, a);
        }
    }

    // Rivara's longest-edge propagation path bisection until no edge is
    // longer than `size`.
    void refine(double size)
    {
        std::vector<int> work;
        for (int t = 0; t < triangleCount(); ++t) {
            if (alive(t)) {
                work.push_back(t);
            }
        }
        while (!work.empty()) {
            const int start = work.back();
            work.pop_back();
            if (!alive(start) || longest(start).second <= size) {
                continue;
            }
            int t = start;
            while (true) {
                const int k = longest(t).first;
                const int a = vertex(t, k);
                const int b = vertex(t, k + 1);
                const int next = across(t, a, b);
                if (next < 0 || sameEdge(next, a, b)) {
                    const std::vector<int> created = split(a, b);
                    work.insert(work.end(), created.begin(), created.end());
                    break;
                }
                t = next;
            }
            work.push_back(start); // refined again if still alive
        }
    }

    [[nodiscard]] SectionMesh finish() const
    {
        SectionMesh mesh;
        mesh.nodes = m_nodes;
        std::unordered_map<std::uint64_t, int> middles;
        for (int t = 0; t < triangleCount(); ++t) {
            if (!alive(t)) {
                continue;
            }
            std::array<int, 6> six{};
            for (int k = 0; k < 3; ++k) {
                six.at(static_cast<std::size_t>(k)) = vertex(t, k);
            }
            for (int k = 0; k < 3; ++k) {
                const int a = vertex(t, k);
                const int b = vertex(t, k + 1);
                const auto [it, added] =
                    middles.try_emplace(edgeKey(a, b), static_cast<int>(mesh.nodes.size()));
                if (added) {
                    mesh.nodes.emplace_back((node(a) + node(b)) / 2.0);
                }
                six.at(3 + static_cast<std::size_t>(k)) = it->second;
            }
            mesh.triangles.push_back(six);
        }
        return mesh;
    }

private:
    [[nodiscard]] bool runs(int t, int a, int b) const
    {
        for (int k = 0; k < 3; ++k) {
            if (vertex(t, k) == a && vertex(t, k + 1) == b) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int opposite(int t, int a, int b) const
    {
        for (int k = 0; k < 3; ++k) {
            const int v = vertex(t, k);
            if (v != a && v != b) {
                return v;
            }
        }
        return -1;
    }

    static double inCircle(const Vec& a, const Vec& b, const Vec& c, const Vec& d)
    {
        const Vec ad = a - d;
        const Vec bd = b - d;
        const Vec cd = c - d;
        return ad.squaredNorm() * cross(bd, cd) - bd.squaredNorm() * cross(ad, cd) +
               cd.squaredNorm() * cross(ad, bd);
    }

    // The longest edge (local index, length); ties go to the smaller key,
    // so that neighbours agree.
    [[nodiscard]] std::pair<int, double> longest(int t) const
    {
        int best = 0;
        double bestLength = -1.0;
        std::uint64_t bestKey = 0;
        for (int k = 0; k < 3; ++k) {
            const int a = vertex(t, k);
            const int b = vertex(t, k + 1);
            const double length = (node(b) - node(a)).norm();
            const std::uint64_t key = edgeKey(a, b);
            if (length > bestLength || (length == bestLength && key < bestKey)) {
                best = k;
                bestLength = length;
                bestKey = key;
            }
        }
        return {best, bestLength};
    }

    [[nodiscard]] bool sameEdge(int t, int a, int b) const
    {
        const int k = longest(t).first;
        return edgeKey(vertex(t, k), vertex(t, k + 1)) == edgeKey(a, b);
    }

    // Bisects the edge ab in the one or two triangles on it.
    std::vector<int> split(int a, int b)
    {
        const auto found = m_edges.find(edgeKey(a, b));
        const std::array<int, 2> owners = found->second;
        const int m = addNode((node(a) + node(b)) / 2.0);
        const bool constrained = m_constrained.contains(edgeKey(a, b));
        if (constrained) {
            constrain(a, m);
            constrain(m, b);
        }
        std::vector<int> created;
        for (const int t : owners) {
            if (t < 0) {
                continue;
            }
            const int p = runs(t, a, b) ? a : b;
            const int q = p == a ? b : a;
            const int r = opposite(t, a, b);
            removeTriangle(t);
            created.push_back(addTriangle(p, m, r));
            created.push_back(addTriangle(m, q, r));
        }
        return created;
    }

    double m_scale;
    std::vector<Vec> m_nodes;
    std::vector<std::array<int, 3>> m_triangles;
    std::vector<bool> m_alive;
    std::unordered_map<std::uint64_t, std::array<int, 2>> m_edges;
    std::unordered_set<std::uint64_t> m_constrained;
};

// A ring without the vertices on the straight line of their neighbours,
// counterclockwise or clockwise as asked.
std::vector<Vec> cleanRing(const Ring& ring, bool counterclockwise, double tol)
{
    std::vector<Vec> out;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec& prev = ring[(i + n - 1) % n];
        const Vec& cur = ring[i];
        const Vec& next = ring[(i + 1) % n];
        if (std::abs(orient(prev, cur, next)) > tol * (next - prev).norm()) {
            out.push_back(cur);
        }
    }
    if ((signedArea(out) > 0.0) != counterclockwise) {
        std::ranges::reverse(out);
    }
    return out;
}

// One region: its outline and holes merged by bridges into one polygon of
// node indices, then clipped ear by ear.
class EarClipper
{
public:
    EarClipper(Triangulation& mesh, double tol)
        : m_mesh(mesh)
        , m_tol(tol)
    {}

    void region(const SolidPolygon::Region& region)
    {
        std::vector<int> polygon = ringNodes(cleanRing(region.outline, true, m_tol));
        std::vector<std::vector<int>> holes;
        holes.reserve(region.holes.size());
        for (const Ring& hole : region.holes) {
            holes.push_back(ringNodes(cleanRing(hole, false, m_tol)));
        }
        // Holes with the largest x first.
        std::ranges::sort(holes, [this](const std::vector<int>& a, const std::vector<int>& b) {
            return maxX(a) > maxX(b);
        });
        for (std::size_t h = 0; h < holes.size(); ++h) {
            bridge(polygon, holes[h], std::span(holes).subspan(h + 1));
        }
        clip(polygon);
    }

private:
    std::vector<int> ringNodes(const std::vector<Vec>& ring)
    {
        std::vector<int> nodes;
        nodes.reserve(ring.size());
        for (const Vec& point : ring) {
            nodes.push_back(m_mesh.addNode(point));
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            m_mesh.constrain(nodes[i], nodes[(i + 1) % nodes.size()]);
        }
        return nodes;
    }

    [[nodiscard]] double maxX(const std::vector<int>& ring) const
    {
        double best = -1e300;
        for (const int v : ring) {
            best = std::max(best, m_mesh.node(v).x());
        }
        return best;
    }

    // Whether the segment pq meets an edge of the ring, other than at p or q.
    [[nodiscard]] bool blocks(const std::vector<int>& ring, int p, int q) const
    {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const int a = ring[i];
            const int b = ring[(i + 1) % ring.size()];
            if (a == p || a == q || b == p || b == q) {
                continue;
            }
            if (segmentsMeet(m_mesh.node(p), m_mesh.node(q), m_mesh.node(a), m_mesh.node(b),
                             m_tol)) {
                return true;
            }
        }
        return false;
    }

    // Joins the hole to the polygon by the shortest visible bridge from its
    // vertex of largest x to a vertex of the polygon used once.
    void bridge(std::vector<int>& polygon, const std::vector<int>& hole,
                std::span<const std::vector<int>> later)
    {
        std::size_t from = 0;
        for (std::size_t i = 1; i < hole.size(); ++i) {
            if (m_mesh.node(hole[i]).x() > m_mesh.node(hole[from]).x()) {
                from = i;
            }
        }
        const int m = hole[from];
        std::size_t best = polygon.size();
        double bestDistance = 1e300;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const int p = polygon[i];
            if (std::ranges::count(polygon, p) > 1) {
                continue; // already the end of a bridge
            }
            const double distance = (m_mesh.node(p) - m_mesh.node(m)).norm();
            if (distance >= bestDistance || !inCorner(polygon, i, m) || blocks(polygon, p, m) ||
                blocks(hole, p, m)) {
                continue;
            }
            const bool hidden = std::ranges::any_of(
                later, [&](const std::vector<int>& other) { return blocks(other, p, m); });
            if (!hidden) {
                best = i;
                bestDistance = distance;
            }
        }
        // A valid region always has a visible vertex; keep the first one
        // otherwise rather than lose the hole.
        best = best == polygon.size() ? 0 : best;
        std::vector<int> merged(polygon.begin(),
                                polygon.begin() + static_cast<std::ptrdiff_t>(best) + 1);
        for (std::size_t k = 0; k <= hole.size(); ++k) {
            merged.push_back(hole[(from + k) % hole.size()]);
        }
        merged.insert(merged.end(), polygon.begin() + static_cast<std::ptrdiff_t>(best),
                      polygon.end());
        polygon = std::move(merged);
    }

    // Whether the direction from the vertex i of the polygon to q is inside
    // the polygon's angle at i.
    [[nodiscard]] bool inCorner(const std::vector<int>& polygon, std::size_t i, int q) const
    {
        const std::size_t n = polygon.size();
        const Vec& prev = m_mesh.node(polygon[(i + n - 1) % n]);
        const Vec& cur = m_mesh.node(polygon[i]);
        const Vec& next = m_mesh.node(polygon[(i + 1) % n]);
        const Vec& target = m_mesh.node(q);
        if (orient(prev, cur, next) >= 0.0) { // convex
            return orient(cur, next, target) > 0.0 && orient(prev, cur, target) > 0.0;
        }
        return orient(cur, next, target) > 0.0 || orient(prev, cur, target) > 0.0;
    }

    void clip(std::vector<int> polygon)
    {
        while (polygon.size() > 3) {
            const std::size_t n = polygon.size();
            std::size_t ear = n;
            double bestQuality = -1.0;
            for (std::size_t i = 0; i < n; ++i) {
                const int a = polygon[(i + n - 1) % n];
                const int b = polygon[i];
                const int c = polygon[(i + 1) % n];
                if (orient(m_mesh.node(a), m_mesh.node(b), m_mesh.node(c)) <= 0.0) {
                    continue;
                }
                if (containsVertex(polygon, a, b, c)) {
                    continue;
                }
                // Prefer the ear of best shape: fewer slivers to flip later.
                const double q = quality(a, b, c);
                if (q > bestQuality) {
                    bestQuality = q;
                    ear = i;
                }
            }
            if (ear == n) {
                ear = 0; // degenerate: should not happen for a checked polygon
            }
            const int a = polygon[ear == 0 ? n - 1 : ear - 1];
            const int b = polygon[ear];
            const int c = polygon[ear + 1 == n ? 0 : ear + 1];
            m_mesh.addTriangle(a, b, c);
            polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(ear));
        }
        m_mesh.addTriangle(polygon[0], polygon[1], polygon[2]);
    }

    [[nodiscard]] bool containsVertex(const std::vector<int>& polygon, int a, int b, int c) const
    {
        const Vec& pa = m_mesh.node(a);
        const Vec& pb = m_mesh.node(b);
        const Vec& pc = m_mesh.node(c);
        for (const int v : polygon) {
            if (v == a || v == b || v == c) {
                continue;
            }
            const Vec& p = m_mesh.node(v);
            if (orient(pa, pb, p) >= 0.0 && orient(pb, pc, p) >= 0.0 && orient(pc, pa, p) >= 0.0) {
                return true;
            }
        }
        return false;
    }

    // Twice the area over the sum of the squared sides: largest for the
    // equilateral triangle.
    [[nodiscard]] double quality(int a, int b, int c) const
    {
        const Vec& pa = m_mesh.node(a);
        const Vec& pb = m_mesh.node(b);
        const Vec& pc = m_mesh.node(c);
        return orient(pa, pb, pc) /
               ((pb - pa).squaredNorm() + (pc - pb).squaredNorm() + (pa - pc).squaredNorm());
    }

    Triangulation& m_mesh;
    double m_tol;
};

} // namespace

double defaultMeshSize(double area)
{
    return std::sqrt(area / 1500.0);
}

double polygonArea(const SolidPolygon& shape)
{
    double area = 0.0;
    for (const auto& region : shape.regions) {
        area += std::abs(signedArea(region.outline));
        for (const Ring& hole : region.holes) {
            area -= std::abs(signedArea(hole));
        }
    }
    return area;
}

Expected<SectionMesh> meshSection(const SolidPolygon& shape, std::string_view section)
{
    Checker checker(shape, section);
    if (auto problem = checker.check()) {
        return makeError(*problem);
    }
    double extent = 0.0;
    for (const auto& region : shape.regions) {
        for (const Vec& p : region.outline) {
            extent = std::max(extent, p.cwiseAbs().maxCoeff());
        }
    }
    const double tol = 1e-10 * extent;
    const double size = shape.meshSize > 0.0 ? shape.meshSize : defaultMeshSize(polygonArea(shape));
    Triangulation mesh(extent);
    EarClipper clipper(mesh, tol);
    for (const auto& region : shape.regions) {
        clipper.region(region);
    }
    mesh.delaunay();
    mesh.refine(size);
    return mesh.finish();
}

} // namespace fem::core::detail
