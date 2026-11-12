#pragma once

#include <fem/core/mesh.hpp>
#include <fem/render/display_options.hpp>

#include <span>
#include <vector>

namespace fem::render {

// Selected elements or nodes, by internal index (REQ-004-19, 24, 25, 28).
class Selection
{
public:
    // Empties the selection and sizes it for `mode` on `mesh`.
    void reset(SelectionMode mode, const core::Mesh& mesh);

    [[nodiscard]] SelectionMode mode() const noexcept { return m_mode; }
    [[nodiscard]] int count() const noexcept { return m_count; }
    [[nodiscard]] bool contains(int index) const noexcept;
    [[nodiscard]] std::vector<int> items() const;

    void add(std::span<const int> indices);
    void remove(std::span<const int> indices);
    void clear();

    // Takes out the selected elements of `dimension` (elements mode only).
    void removeElementsOfDimension(const core::Mesh& mesh, int dimension);

private:
    SelectionMode m_mode = SelectionMode::Elements;
    std::vector<bool> m_selected;
    int m_count = 0;
};

} // namespace fem::render
