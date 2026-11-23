#pragma once

// Feature 010 (design §5.6; REQ-010-09, 22): the symbols of the model for
// the viewport: the supports, the loads of the active load case or
// combination and the local axes of the beams. On the mesh, from the model
// built (node positions); without a mesh, from the entities of the drawing
// that items target, so that they show before MESH.

#include <fem/geom/drawing.hpp>
#include <fem/render/symbols.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace fem::app {

// REQ-010-09: the local axes of none, of the bars of `axesOf`, or of all.
enum class AxesShown : std::uint8_t { None, Selected, All };

struct SymbolOptions
{
    AxesShown axes = AxesShown::None;
    // Selected: the bars of these targets (the viewport selection).
    setup::Targets axesOf;
};

[[nodiscard]] render::SymbolSet buildSymbols(const setup::Document& document,
                                             const geom::Drawing& drawing,
                                             const setup::MeshContext& mesh,
                                             const SymbolOptions& options);

} // namespace fem::app
