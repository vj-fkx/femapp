#include <fem/core/mesh_builder.hpp>
#include <fem/mesher/line_mesher.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace fem::mesher {

namespace {

using geom::Vec3;

// A straight piece of an entity (design §5.1).
struct Piece
{
    std::size_t entity = 0; // index in the list of entities meshed
    int segment = 0;        // index in geom::segmentsOf
    Vec3 a = Vec3::Zero();
    Vec3 b = Vec3::Zero();
    std::int32_t startCandidate = 0;
    std::int32_t endCandidate = 0;
    bool ignored = false;
};

// A node candidate (design §5.4): an end, a vertex, a point or a split.
struct Candidate
{
    Vec3 position = Vec3::Zero();
    std::size_t entity = 0;
    int vertex = -1; // geom::verticesOf index; -1 for a split
};

// Union-find whose root is the smallest index: the first candidate in order
// gives the node its position (design §5.4).
class Merge
{
public:
    explicit Merge(std::size_t count)
        : m_parent(count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            m_parent[i] = static_cast<std::int32_t>(i);
        }
    }

    std::int32_t root(std::int32_t i)
    {
        while (m_parent[static_cast<std::size_t>(i)] != i) {
            auto& p = m_parent[static_cast<std::size_t>(i)];
            p = m_parent[static_cast<std::size_t>(p)];
            i = p;
        }
        return i;
    }

    void join(std::int32_t a, std::int32_t b)
    {
        a = root(a);
        b = root(b);
        if (a != b) {
            m_parent[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
        }
    }

private:
    std::vector<std::int32_t> m_parent;
};

struct CellHash
{
    std::size_t operator()(const std::array<std::int64_t, 3>& c) const noexcept
    {
        auto h = static_cast<std::uint64_t>(c[0]) * 0x9E3779B97F4A7C15ULL;
        h ^= static_cast<std::uint64_t>(c[1]) * 0xC2B2AE3D27D4EB4FULL + (h << 6U) + (h >> 2U);
        h ^= static_cast<std::uint64_t>(c[2]) * 0x165667B19E3779F9ULL + (h << 6U) + (h >> 2U);
        return static_cast<std::size_t>(h);
    }
};

// Joins the candidates closer than `tolerance`, through a hash grid of
// cells of that size (design §5.4).
void mergeCandidates(const std::vector<Candidate>& candidates, double tolerance, Merge& merge)
{
    if (candidates.empty()) {
        return;
    }
    Vec3 origin = candidates.front().position;
    for (const Candidate& c : candidates) {
        origin = origin.cwiseMin(c.position);
    }
    const auto cellOf = [&](const Vec3& p) {
        const Vec3 q = (p - origin) / tolerance;
        return std::array<std::int64_t, 3>{static_cast<std::int64_t>(std::floor(q.x())),
                                           static_cast<std::int64_t>(std::floor(q.y())),
                                           static_cast<std::int64_t>(std::floor(q.z()))};
    };
    std::unordered_map<std::array<std::int64_t, 3>, std::vector<std::int32_t>, CellHash> grid;
    grid.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Vec3& p = candidates[i].position;
        const auto cell = cellOf(p);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const auto found = grid.find({cell[0] + dx, cell[1] + dy, cell[2] + dz});
                    if (found == grid.end()) {
                        continue;
                    }
                    for (const std::int32_t j : found->second) {
                        if ((candidates[static_cast<std::size_t>(j)].position - p).norm() <=
                            tolerance) {
                            merge.join(static_cast<std::int32_t>(i), j);
                        }
                    }
                }
            }
        }
        grid[cell].push_back(static_cast<std::int32_t>(i));
    }
}

bool isPoint(const geom::Entity& entity)
{
    return std::holds_alternative<geom::PointEntity>(entity.shape);
}

// REQ-009-01: the entities meshed, in drawing order.
tl::expected<std::vector<const geom::Entity*>, LineMeshError>
entitiesOf(const geom::Drawing& drawing, const LineMeshOptions& options)
{
    std::vector<const geom::Entity*> out;
    if (options.entities.empty()) {
        for (const geom::Entity& e : drawing.entities()) {
            out.push_back(&e);
        }
        return out;
    }
    LineMeshError unknown{.kind = LineMeshError::Kind::UnknownEntity};
    std::vector<std::uint64_t> wanted;
    for (const geom::EntityId id : options.entities) {
        if (drawing.find(id) == nullptr) {
            unknown.items.push_back(id);
        }
        wanted.push_back(static_cast<std::uint64_t>(id));
    }
    if (!unknown.items.empty()) {
        return tl::unexpected(std::move(unknown));
    }
    std::ranges::sort(wanted);
    for (const geom::Entity& e : drawing.entities()) {
        if (std::ranges::binary_search(wanted, static_cast<std::uint64_t>(e.id))) {
            out.push_back(&e);
        }
    }
    return out;
}

// Builds the mesh in the order of design §5.6.
class Numbering
{
public:
    explicit Numbering(std::size_t candidates)
        : m_nodeOfRoot(candidates, -1)
    {}

    // The node of a candidate root; numbered on first use.
    std::int32_t node(std::int32_t root, const Vec3& position)
    {
        auto& n = m_nodeOfRoot[static_cast<std::size_t>(root)];
        if (n < 0) {
            n = addNode(position);
            m_rootOfNode.push_back(root);
        }
        return n;
    }

    std::int32_t interiorNode(const Vec3& position)
    {
        const std::int32_t n = addNode(position);
        m_rootOfNode.push_back(-1);
        return n;
    }

    // An element of `entity` through the nodes, with its id.
    void element(core::ElementType type, std::span<const std::int32_t> nodes, std::size_t entity,
                 int segment)
    {
        Element e{.type = type, .entity = entity, .segment = segment, .nodes = {}};
        for (const std::int32_t n : nodes) {
            e.nodes.push_back(n);
            auto& through = m_entitiesOfNode[static_cast<std::size_t>(n)];
            if (through.empty() || through.back() != entity) {
                through.push_back(entity);
            }
        }
        m_elements.push_back(std::move(e));
    }

    struct Element
    {
        core::ElementType type = core::ElementType::Line2;
        std::size_t entity = 0;
        int segment = 0;
        std::vector<std::int32_t> nodes;
    };

    [[nodiscard]] const std::vector<Vec3>& positions() const { return m_positions; }
    [[nodiscard]] const std::vector<Element>& elements() const { return m_elements; }
    [[nodiscard]] const std::vector<std::vector<std::size_t>>& entitiesOfNode() const
    {
        return m_entitiesOfNode;
    }
    [[nodiscard]] const std::vector<std::int32_t>& rootOfNode() const { return m_rootOfNode; }

private:
    std::int32_t addNode(const Vec3& position)
    {
        m_positions.push_back(position);
        m_entitiesOfNode.emplace_back();
        return static_cast<std::int32_t>(m_positions.size() - 1);
    }

    std::vector<std::int32_t> m_nodeOfRoot;
    std::vector<std::int32_t> m_rootOfNode;
    std::vector<Vec3> m_positions;
    std::vector<std::vector<std::size_t>> m_entitiesOfNode;
    std::vector<Element> m_elements;
};

core::ExternalId externalId(std::size_t index)
{
    return static_cast<core::ExternalId>(index) + 1;
}

template <typename T>
T check(core::Expected<T> value)
{
    // The data come from the generator: a refusal is a programming error
    // (design §7).
    if (!value) {
        throw std::logic_error("line mesher: " + value.error().message);
    }
    if constexpr (!std::is_void_v<T>) {
        return std::move(*value);
    }
}

} // namespace

std::string entityGroupName(geom::EntityId id)
{
    return "entity " + std::to_string(static_cast<std::uint64_t>(id));
}

tl::expected<LineMeshResult, LineMeshError> meshLines(const geom::Drawing& drawing,
                                                      const LineMeshOptions& options)
{
    auto entities = entitiesOf(drawing, options);
    if (!entities) {
        return tl::unexpected(std::move(entities.error()));
    }
    const double tolerance = drawing.tolerance();
    LineMeshResult result;

    // 1. Pieces and node candidates (design §5.1, §5.4).
    std::vector<Candidate> candidates;
    std::vector<Piece> pieces;
    std::vector<std::int32_t> pointCandidate(entities->size(), -1);
    bool anyPiece = false;
    for (std::size_t e = 0; e < entities->size(); ++e) {
        const geom::Entity& entity = *(*entities)[e];
        const auto first = static_cast<std::int32_t>(candidates.size());
        const std::vector<Vec3> vertices = geom::verticesOf(entity);
        for (std::size_t v = 0; v < vertices.size(); ++v) {
            candidates.push_back(
                {.position = vertices[v], .entity = e, .vertex = static_cast<int>(v)});
        }
        if (isPoint(entity)) {
            pointCandidate[e] = first;
            continue;
        }
        const auto count = static_cast<std::int32_t>(vertices.size());
        const std::vector<geom::Segment> segments = geom::segmentsOf(entity);
        for (std::size_t s = 0; s < segments.size(); ++s) {
            const auto k = static_cast<std::int32_t>(s);
            const Piece piece{.entity = e,
                              .segment = k,
                              .a = segments[s].a,
                              .b = segments[s].b,
                              .startCandidate = first + k,
                              .endCandidate = first + (k + 1) % count,
                              .ignored = (segments[s].b - segments[s].a).norm() <= tolerance};
            if (piece.ignored) {
                result.report.ignored.emplace_back(entity.id, k);
            } else {
                anyPiece = true;
            }
            pieces.push_back(piece);
        }
    }
    if (!anyPiece) {
        return tl::unexpected(LineMeshError{.kind = LineMeshError::Kind::NoLines});
    }

    // 2. Merge (design §5.4).
    Merge merge(candidates.size());
    mergeCandidates(candidates, tolerance, merge);

    // 3. Numbering along the entities (design §5.6): one element per piece.
    Numbering numbering(candidates.size());
    const auto nodeOf = [&](std::int32_t candidate) {
        const std::int32_t root = merge.root(candidate);
        return numbering.node(root, candidates[static_cast<std::size_t>(root)].position);
    };
    std::size_t next = 0;
    for (std::size_t e = 0; e < entities->size(); ++e) {
        if (pointCandidate[e] >= 0) {
            const std::array<std::int32_t, 1> node{nodeOf(pointCandidate[e])};
            numbering.element(core::ElementType::Point1, node, e, 0);
            continue;
        }
        for (; next < pieces.size() && pieces[next].entity == e; ++next) {
            const Piece& piece = pieces[next];
            if (piece.ignored) {
                continue;
            }
            const std::array<std::int32_t, 2> nodes{nodeOf(piece.startCandidate),
                                                    nodeOf(piece.endCandidate)};
            numbering.element(core::ElementType::Line2, nodes, e, piece.segment);
        }
    }

    // 4. The mesh, its groups and the map (design §5.6).
    core::MeshBuilder builder;
    const auto& positions = numbering.positions();
    builder.reserveNodes(positions.size());
    for (std::size_t n = 0; n < positions.size(); ++n) {
        check(builder.addNode(externalId(n), positions[n]));
    }
    std::vector<std::vector<core::ElementHandle>> groups(entities->size());
    std::vector<core::ExternalId> nodeIds;
    const auto& elements = numbering.elements();
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];
        nodeIds.clear();
        for (const std::int32_t n : element.nodes) {
            nodeIds.push_back(externalId(static_cast<std::size_t>(n)));
        }
        groups[element.entity].push_back(
            check(builder.addElement(element.type, externalId(i), nodeIds)));
    }
    for (std::size_t e = 0; e < entities->size(); ++e) {
        if (!groups[e].empty()) {
            check(builder.addGroup(entityGroupName((*entities)[e]->id),
                                   pointCandidate[e] >= 0 ? 0 : 1, std::move(groups[e])));
        }
    }
    result.mesh = check(builder.build());

    result.map.elements.resize(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto index = result.mesh.findElement(externalId(i));
        if (!index) {
            throw std::logic_error("line mesher: an element was lost");
        }
        result.map.elements[static_cast<std::size_t>(*index)] = {
            .entity = (*entities)[elements[i].entity]->id, .segment = elements[i].segment};
    }
    result.map.nodes.resize(positions.size());
    for (std::size_t n = 0; n < positions.size(); ++n) {
        const auto index = result.mesh.findNode(externalId(n));
        if (!index) {
            throw std::logic_error("line mesher: a node was lost");
        }
        NodeOrigin& origin = result.map.nodes[static_cast<std::size_t>(*index)];
        // In drawing order, once each: the elements are made entity by
        // entity, in that order.
        for (const std::size_t e : numbering.entitiesOfNode()[n]) {
            origin.entities.push_back((*entities)[e]->id);
        }
        if (const std::int32_t root = numbering.rootOfNode()[n]; root >= 0) {
            const Candidate& c = candidates[static_cast<std::size_t>(root)];
            if (c.vertex >= 0) {
                origin.vertex = std::pair{(*entities)[c.entity]->id, c.vertex};
            }
        }
    }

    // The report (REQ-009-11).
    result.report.nodes = result.mesh.nodeCount();
    result.report.elements = result.mesh.elementCount();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (merge.root(static_cast<std::int32_t>(i)) != static_cast<std::int32_t>(i)) {
            ++result.report.mergedNodes;
        }
    }
    return result;
}

} // namespace fem::mesher
