#include <fem/geom/drawing.hpp>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace fem::geom {

namespace {

std::uint64_t key(EntityId id) noexcept
{
    return static_cast<std::uint64_t>(id);
}

constexpr double kRelativeTolerance = 1e-9;

} // namespace

void DrawingEdit::append(const DrawingEdit& next)
{
    for (const Entity& entity : next.removed) {
        // Something created (or changed) by this edit is taken out again: it
        // simply no longer appears among the added ones.
        const auto created = std::ranges::find(added, entity.id, &Entity::id);
        if (created != added.end()) {
            added.erase(created);
        } else {
            removed.push_back(entity);
        }
    }
    added.insert(added.end(), next.added.begin(), next.added.end());
}

EntityId Drawing::nextId() noexcept
{
    return EntityId{++m_lastId};
}

const Entity* Drawing::find(EntityId id) const
{
    const auto found = m_index.find(key(id));
    return found == m_index.end() ? nullptr : &m_entities[found->second];
}

std::optional<Box> Drawing::bounds() const
{
    std::optional<Box> box;
    for (const Entity& entity : m_entities) {
        for (const Vec3& p : verticesOf(entity)) {
            if (!box) {
                box = Box{.min = p, .max = p};
            } else {
                box->min = box->min.cwiseMin(p);
                box->max = box->max.cwiseMax(p);
            }
        }
    }
    return box;
}

double Drawing::tolerance() const
{
    const auto box = bounds();
    const double size = box ? (box->max - box->min).norm() : 0.0;
    return kRelativeTolerance * std::max(1.0, size);
}

void Drawing::apply(const DrawingEdit& edit)
{
    for (const Entity& entity : edit.removed) {
        erase(entity.id);
    }
    for (const Entity& entity : edit.added) {
        insert(entity);
    }
    ++m_revision;
}

void Drawing::revert(const DrawingEdit& edit)
{
    for (const Entity& entity : edit.added) {
        erase(entity.id);
    }
    for (const Entity& entity : edit.removed) {
        insert(entity);
    }
    ++m_revision;
}

void Drawing::insert(const Entity& entity)
{
    const auto found = m_index.find(key(entity.id));
    if (found != m_index.end()) {
        m_entities[found->second] = entity;
        return;
    }
    m_index.emplace(key(entity.id), m_entities.size());
    m_entities.push_back(entity);
    // Ids coming back with an undo never lower the counter.
    m_lastId = std::max(m_lastId, key(entity.id));
}

void Drawing::erase(EntityId id)
{
    const auto found = m_index.find(key(id));
    if (found == m_index.end()) {
        return;
    }
    const std::size_t index = found->second;
    m_index.erase(found);
    if (index + 1 != m_entities.size()) {
        m_entities[index] = std::move(m_entities.back());
        m_index[key(m_entities[index].id)] = index;
    }
    m_entities.pop_back();
}

std::vector<Segment> segmentsOf(const Entity& entity)
{
    std::vector<Segment> segments;
    std::visit(
        [&](const auto& shape) {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, LineEntity>) {
                segments.push_back({.a = shape.start, .b = shape.end, .entity = entity.id});
            } else if constexpr (std::is_same_v<Shape, PolylineEntity>) {
                const std::size_t n = shape.vertices.size();
                for (std::size_t i = 0; i + 1 < n; ++i) {
                    segments.push_back(
                        {.a = shape.vertices[i], .b = shape.vertices[i + 1], .entity = entity.id});
                }
                if (shape.closed && n > 2) {
                    segments.push_back(
                        {.a = shape.vertices[n - 1], .b = shape.vertices[0], .entity = entity.id});
                }
            }
        },
        entity.shape);
    return segments;
}

std::vector<Vec3> verticesOf(const Entity& entity)
{
    return std::visit(
        [](const auto& shape) -> std::vector<Vec3> {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, PointEntity>) {
                return {shape.position};
            } else if constexpr (std::is_same_v<Shape, LineEntity>) {
                return {shape.start, shape.end};
            } else {
                return shape.vertices;
            }
        },
        entity.shape);
}

Entity translated(const Entity& entity, const Vec3& by, EntityId id)
{
    Entity result{.id = id, .shape = entity.shape};
    std::visit(
        [&](auto& shape) {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, PointEntity>) {
                shape.position += by;
            } else if constexpr (std::is_same_v<Shape, LineEntity>) {
                shape.start += by;
                shape.end += by;
            } else {
                for (Vec3& v : shape.vertices) {
                    v += by;
                }
            }
        },
        result.shape);
    return result;
}

} // namespace fem::geom
