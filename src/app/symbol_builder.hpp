#pragma once

// Feature 010 (design §5.6; REQ-010-09, 22): the symbols of the model for
// the viewport: the supports, the loads of the active load case or
// combination and the local axes of the beams. On the mesh, from the model
// built (node positions); without a mesh, from the entities of the drawing
// that items target, so that they show before MESH.

#include <fem/geom/drawing.hpp>
#include <fem/render/symbols.hpp>
#include <fem/setup/analysis.hpp>
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
    // REQ-012-03: false while a result is shown: the supports stay, the
    // loads go.
    bool loadsShown = true;
    // REQ-012-16: the reactions of the result shown (reactionSymbols).
    std::vector<render::ArrowSymbol> reactionForces;
    std::vector<render::MomentSymbol> reactionMoments;
};

[[nodiscard]] render::SymbolSet buildSymbols(const setup::Document& document,
                                             const geom::Drawing& drawing,
                                             const setup::MeshContext& mesh,
                                             const SymbolOptions& options);

// REQ-012-03, 16: the symbols of the model `base` (built with the loads and
// without reactions) as the display of a result shows them: without the
// loads when they are hidden, with the reactions. No rebuild of the model.
[[nodiscard]] render::SymbolSet withResults(const render::SymbolSet& base,
                                            const SymbolOptions& options);

// REQ-012-16: the reactions of a linear or second-order result at its
// supported nodes: one arrow per force component and one arc per moment
// component that is not negligible (1e-9 of the largest of its kind), in
// the sense of the reaction, the arrow ending at the node. Nothing for a
// buckling result.
struct ReactionSymbols
{
    std::vector<render::ArrowSymbol> forces;
    std::vector<render::MomentSymbol> moments;
};
[[nodiscard]] ReactionSymbols reactionSymbols(const setup::AnalysisResult& result);

} // namespace fem::app
