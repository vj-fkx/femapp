#pragma once

#include <fem/core/mesh.hpp>
#include <fem/render/display_options.hpp>

#include <array>
#include <vector>

namespace fem::render {

class MeshScene;

// Items by kind: element, outer face (index in MeshSurface::faces3d), outer
// edge (index in MeshSurface::edges3d) and node, by internal index.
struct SelectionItems
{
    std::array<std::vector<int>, kSelectionKindCount> byKind;

    [[nodiscard]] std::vector<int>& operator[](SelectionKind kind) { return byKind[toIndex(kind)]; }
    [[nodiscard]] const std::vector<int>& operator[](SelectionKind kind) const
    {
        return byKind[toIndex(kind)];
    }
    [[nodiscard]] bool empty() const noexcept;
};

// Selected items of every kind (REQ-004-19, 24, 25, 28, 33).
class Selection
{
public:
    // Empties the selection and sizes it for `scene`; nullptr: nothing.
    void reset(const MeshScene* scene);

    [[nodiscard]] int count(SelectionKind kind) const noexcept
    {
        return m_sets[toIndex(kind)].count;
    }
    [[nodiscard]] int total() const noexcept;
    [[nodiscard]] bool contains(SelectionKind kind, int index) const noexcept;
    [[nodiscard]] std::vector<int> items(SelectionKind kind) const;

    void add(const SelectionItems& items);
    void remove(const SelectionItems& items);
    void clear();
    // REQ-004-33
    void clear(SelectionKind kind);

    // REQ-004-28: takes out the selected elements and edges of `dimension`;
    // for 3D, the faces of the solids too.
    void removeElementsOfDimension(const MeshScene& scene, int dimension);

private:
    struct Set
    {
        std::vector<bool> selected;
        int count = 0;
    };
    std::array<Set, kSelectionKindCount> m_sets;
};

} // namespace fem::render
