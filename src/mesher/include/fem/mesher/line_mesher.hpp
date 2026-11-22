#pragma once

// Feature 009: the bars of a drawing as a mesh (design 009 §4, §5).

#include <fem/core/mesh.hpp>
#include <fem/geom/drawing.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fem::mesher {

// REQ-009-06: where a geometric grading makes the elements grow.
enum class Grading : std::uint8_t {
    TowardEnd, // the last element is `ratio` times the first, from start to end
    TowardBoth // symmetric: the largest (middle) element is `ratio` times the end ones
};

// REQ-009-05 to 07: how the pieces of an entity are divided. Without
// divisions or length: one element per piece.
struct LineSizing
{
    std::optional<int> divisions; // per piece, >= 1
    std::optional<double> length; // target length, > 0 (divisions win if both)
    double ratio = 1.0;           // > 0; 1 is uniform
    Grading grading = Grading::TowardEnd;
    bool quadratic = false; // REQ-009-07: Line3 instead of Line2
};

struct LineMeshOptions
{
    LineSizing defaults{};
    // REQ-009-06: per entity, over the defaults (by the value of the id).
    std::unordered_map<std::uint64_t, LineSizing> sizing;
    // REQ-009-01: only these entities (all when empty).
    std::vector<geom::EntityId> entities;
    int threads = 0; // 0: all cores
};

// REQ-009-09: the name of the mesh group of an entity, "entity <id>".
[[nodiscard]] std::string entityGroupName(geom::EntityId id);

// REQ-009-10: where an element comes from.
struct ElementOrigin
{
    geom::EntityId entity{};
    int segment = 0; // index in geom::segmentsOf(entity); 0 for a point
};

// REQ-009-10: where a node comes from.
struct NodeOrigin
{
    std::vector<geom::EntityId> entities; // through the node, in drawing order
    // The vertex (index in geom::verticesOf) or the point it comes from.
    std::optional<std::pair<geom::EntityId, int>> vertex;
};

struct LineMeshMap
{
    std::vector<ElementOrigin> elements; // per element (internal index)
    std::vector<NodeOrigin> nodes;       // per node (internal index)
};

// REQ-009-11
enum class SplitKind : std::uint8_t { TJoint, Crossing, PointOnLine };

struct Split
{
    SplitKind kind = SplitKind::Crossing;
    geom::Vec3 position = geom::Vec3::Zero();
    // The entities split, then the one that causes the split.
    std::vector<geom::EntityId> entities;
};

struct LineMeshReport
{
    int nodes = 0;
    int elements = 0;
    std::vector<Split> splits;
    int mergedNodes = 0; // candidates merged into another
    // (entity, segment) shorter than the tolerance of the drawing.
    std::vector<std::pair<geom::EntityId, int>> ignored;
};

struct LineMeshResult
{
    core::Mesh mesh;
    LineMeshMap map;
    LineMeshReport report;
};

// REQ-009-04: two pieces that share a stretch.
struct Overlap
{
    geom::EntityId first{};
    geom::EntityId second{};
    geom::Vec3 from = geom::Vec3::Zero();
    geom::Vec3 to = geom::Vec3::Zero();
};

// REQ-009-14: why a drawing could not be meshed; the app words it.
struct LineMeshError
{
    enum class Kind : std::uint8_t { NoLines, Overlaps, InvalidSizing, UnknownEntity };
    Kind kind = Kind::NoLines;
    std::vector<Overlap> overlaps;     // Overlaps: all of them
    std::vector<geom::EntityId> items; // InvalidSizing, UnknownEntity
};

// REQ-009-01 to 11: the lines and polylines of `drawing` (or of
// `options.entities`) divided into bars, its points as nodes.
[[nodiscard]] tl::expected<LineMeshResult, LineMeshError>
meshLines(const geom::Drawing& drawing, const LineMeshOptions& options = {});

} // namespace fem::mesher
