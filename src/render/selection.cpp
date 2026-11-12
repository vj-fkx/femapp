#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <algorithm>
#include <cstddef>

namespace fem::render {

bool SelectionItems::empty() const noexcept
{
    return std::ranges::all_of(byKind, [](const std::vector<int>& items) { return items.empty(); });
}

void Selection::reset(const MeshScene* scene)
{
    std::array<int, kSelectionKindCount> sizes{};
    if (scene != nullptr) {
        sizes[toIndex(SelectionKind::Element)] = scene->mesh().elementCount();
        sizes[toIndex(SelectionKind::Face)] = static_cast<int>(scene->surface().faces3d.size());
        sizes[toIndex(SelectionKind::Edge)] = static_cast<int>(scene->surface().edges3d.size());
        sizes[toIndex(SelectionKind::Node)] = scene->mesh().nodeCount();
    }
    for (std::size_t k = 0; k < kSelectionKindCount; ++k) {
        m_sets.at(k).selected.assign(static_cast<std::size_t>(sizes.at(k)), false);
        m_sets.at(k).count = 0;
    }
}

int Selection::total() const noexcept
{
    int sum = 0;
    for (const Set& set : m_sets) {
        sum += set.count;
    }
    return sum;
}

bool Selection::contains(SelectionKind kind, int index) const noexcept
{
    const Set& set = m_sets[toIndex(kind)];
    return index >= 0 && static_cast<std::size_t>(index) < set.selected.size() &&
           set.selected[static_cast<std::size_t>(index)];
}

std::vector<int> Selection::items(SelectionKind kind) const
{
    const Set& set = m_sets[toIndex(kind)];
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(set.count));
    for (std::size_t i = 0; i < set.selected.size(); ++i) {
        if (set.selected[i]) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

void Selection::add(const SelectionItems& items)
{
    for (const SelectionKind kind : kSelectionKinds) {
        Set& set = m_sets[toIndex(kind)];
        for (const int index : items[kind]) {
            if (index >= 0 && static_cast<std::size_t>(index) < set.selected.size() &&
                !set.selected[static_cast<std::size_t>(index)]) {
                set.selected[static_cast<std::size_t>(index)] = true;
                ++set.count;
            }
        }
    }
}

void Selection::remove(const SelectionItems& items)
{
    for (const SelectionKind kind : kSelectionKinds) {
        Set& set = m_sets[toIndex(kind)];
        for (const int index : items[kind]) {
            if (contains(kind, index)) {
                set.selected[static_cast<std::size_t>(index)] = false;
                --set.count;
            }
        }
    }
}

void Selection::clear()
{
    for (const SelectionKind kind : kSelectionKinds) {
        clear(kind);
    }
}

void Selection::clear(SelectionKind kind)
{
    Set& set = m_sets[toIndex(kind)];
    set.selected.assign(set.selected.size(), false);
    set.count = 0;
}

void Selection::removeElementsOfDimension(const core::Mesh& mesh, int dimension)
{
    Set& elements = m_sets[toIndex(SelectionKind::Element)];
    for (const core::ElementBlock& block : mesh.blocks()) {
        if (core::elementTypeInfo(block.type).dimension != dimension) {
            continue;
        }
        for (int i = 0; i < block.elementCount(); ++i) {
            const int element = block.firstElement + i;
            if (contains(SelectionKind::Element, element)) {
                elements.selected[static_cast<std::size_t>(element)] = false;
                --elements.count;
            }
        }
    }
    if (dimension == 3) {
        clear(SelectionKind::Face);
        clear(SelectionKind::Edge);
    }
}

} // namespace fem::render
