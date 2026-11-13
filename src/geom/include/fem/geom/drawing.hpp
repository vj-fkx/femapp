#pragma once

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fem::geom {

using Vec3 = Eigen::Vector3d;

// REQ-005-02: stable identity of an entity; 0 is never used.
enum class EntityId : std::uint64_t {};

struct PointEntity
{
    Vec3 position = Vec3::Zero();
};

struct LineEntity
{
    Vec3 start = Vec3::Zero();
    Vec3 end = Vec3::Zero();
};

// Straight pieces only; at least two vertices.
struct PolylineEntity
{
    std::vector<Vec3> vertices;
    bool closed = false;
};

using EntityShape = std::variant<PointEntity, LineEntity, PolylineEntity>;

// REQ-005-01
struct Entity
{
    EntityId id{};
    EntityShape shape;
};

// A straight piece of an entity.
struct Segment
{
    Vec3 a = Vec3::Zero();
    Vec3 b = Vec3::Zero();
    EntityId entity{};
};

struct Box
{
    Vec3 min = Vec3::Zero();
    Vec3 max = Vec3::Zero();
};

// One reversible change of a drawing (REQ-005-31, 32): the entities taken
// out and the entities put in. An entity moved is taken out with its old
// shape and put in with the new one, under the same id.
struct DrawingEdit
{
    std::vector<Entity> removed;
    std::vector<Entity> added;

    [[nodiscard]] bool empty() const noexcept { return removed.empty() && added.empty(); }
    // Folds `next` (applied after this edit) into this one, so that a whole
    // command is a single undo step.
    void append(const DrawingEdit& next);
};

// The drawing document (REQ-005-01, 02). Entities keep their id for ever;
// ids are never reused, also after undoing.
class Drawing
{
public:
    // Reserves a fresh id.
    [[nodiscard]] EntityId nextId() noexcept;

    [[nodiscard]] const Entity* find(EntityId id) const;
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return m_entities; }
    [[nodiscard]] std::size_t size() const noexcept { return m_entities.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_entities.empty(); }
    // Changes on every edit.
    [[nodiscard]] std::uint64_t revision() const noexcept { return m_revision; }
    [[nodiscard]] std::optional<Box> bounds() const;
    // Distance under which two points are the same: 1e-9 x max(1, diagonal).
    [[nodiscard]] double tolerance() const;

    // Removes `edit.removed`, then inserts `edit.added`.
    void apply(const DrawingEdit& edit);
    // The inverse of apply.
    void revert(const DrawingEdit& edit);

private:
    void insert(const Entity& entity);
    void erase(EntityId id);

    std::vector<Entity> m_entities;
    std::unordered_map<std::uint64_t, std::size_t> m_index;
    std::uint64_t m_lastId = 0;
    std::uint64_t m_revision = 0;
};

// Straight pieces: none for a point, one for a line, n - 1 (open) or n
// (closed) for a polyline.
[[nodiscard]] std::vector<Segment> segmentsOf(const Entity& entity);
// Vertices of an entity (the point, both ends, the polyline vertices).
[[nodiscard]] std::vector<Vec3> verticesOf(const Entity& entity);
// A copy of `entity` moved by `by`, with id `id`.
[[nodiscard]] Entity translated(const Entity& entity, const Vec3& by, EntityId id);

} // namespace fem::geom
