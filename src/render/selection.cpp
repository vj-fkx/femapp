#include <fem/render/selection.hpp>

#include <algorithm>
#include <cstddef>

namespace fem::render {

void Selection::reset(SelectionMode mode, const core::Mesh& mesh)
{
    m_mode = mode;
    const int size = mode == SelectionMode::Elements ? mesh.elementCount() : mesh.nodeCount();
    m_selected.assign(static_cast<std::size_t>(size), false);
    m_count = 0;
}

bool Selection::contains(int index) const noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < m_selected.size() &&
           m_selected[static_cast<std::size_t>(index)];
}

std::vector<int> Selection::items() const
{
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(m_count));
    for (std::size_t i = 0; i < m_selected.size(); ++i) {
        if (m_selected[i]) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

void Selection::add(std::span<const int> indices)
{
    for (const int index : indices) {
        if (index >= 0 && static_cast<std::size_t>(index) < m_selected.size() &&
            !m_selected[static_cast<std::size_t>(index)]) {
            m_selected[static_cast<std::size_t>(index)] = true;
            ++m_count;
        }
    }
}

void Selection::remove(std::span<const int> indices)
{
    for (const int index : indices) {
        if (contains(index)) {
            m_selected[static_cast<std::size_t>(index)] = false;
            --m_count;
        }
    }
}

void Selection::clear()
{
    m_selected.assign(m_selected.size(), false);
    m_count = 0;
}

void Selection::removeElementsOfDimension(const core::Mesh& mesh, int dimension)
{
    if (m_mode != SelectionMode::Elements) {
        return;
    }
    for (const core::ElementBlock& block : mesh.blocks()) {
        if (core::elementTypeInfo(block.type).dimension != dimension) {
            continue;
        }
        for (int i = 0; i < block.elementCount(); ++i) {
            const int element = block.firstElement + i;
            if (contains(element)) {
                m_selected[static_cast<std::size_t>(element)] = false;
                --m_count;
            }
        }
    }
}

} // namespace fem::render
