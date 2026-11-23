#pragma once

// Feature 010: the core::Model of a load case or combination, built from
// the document on the current mesh (design 010 §5.3, §5.4; REQ-010-10, 13,
// 14, 17, 20, 21).

#include <fem/core/mesh.hpp>
#include <fem/core/model.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/document.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fem::setup {

// The mesh the model is built on.
struct MeshContext
{
    std::shared_ptr<const core::Mesh> mesh;
    // The map to the drawing (MESH); null for an imported mesh.
    std::shared_ptr<const mesher::LineMeshMap> map;
    // Bumped on every new mesh (REQ-010-13).
    std::uint64_t generation = 0;
    // The revision of the drawing meshed (MESH); none for an imported mesh.
    std::optional<std::uint64_t> drawingRevision;
};

// A load case or a combination (REQ-010-17).
struct LoadSelection
{
    ItemId item{};
};

enum class Severity : std::uint8_t { Error, Warning };

enum class IssueKind : std::uint8_t {
    NoMesh,            // no mesh to build on
    MeshOutOfDate,     // the drawing changed after MESH
    MissingEntity,     // an entity of the targets is not in the drawing
    EntityNotMeshed,   // an entity has nothing in the mesh (or the mesh has no map)
    MissingGroup,      // a group of the targets is not in the mesh
    StaleSelection,    // elements or nodes picked in another mesh
    MissingElement,    // a picked element is not in the mesh
    MissingNode,       // a picked node is not in the mesh
    UnknownMaterial,   // an assignment names a material that is not there
    UnknownSection,    // an assignment names a section that is not there
    UnknownLoad,       // the selection is not a load case or a combination
    SettlementNotHeld, // a settlement on a dof that no support holds
    TrussBuckling,     // REQ-010-10: trusses do not buckle between their nodes
    Core               // an issue of core::validate (REQ-010-21)
};

// REQ-010-20: a problem of the build, with the item involved when there is
// one. `detail` names what is wrong (an entity id, a group, a name).
struct BuildIssue
{
    Severity severity = Severity::Error;
    IssueKind kind = IssueKind::NoMesh;
    std::optional<ItemId> item;
    std::string detail;
};

struct BuildResult
{
    core::Model model;
    std::vector<BuildIssue> issues;
};

// REQ-010-20: the core::Model of `load` on the mesh of `mesh`. Items with
// invalid targets give an issue and are left out; a combination is the sum
// of its cases times their factors, as one set of actions.
[[nodiscard]] BuildResult buildModel(const Document& document, const geom::Drawing& drawing,
                                     const MeshContext& mesh, LoadSelection load);

// REQ-010-13, 14: the state of the targets of an item, for the tree.
enum class TargetState : std::uint8_t { Valid, MissingEntity, StaleSelection, MissingGroup };

[[nodiscard]] TargetState targetState(const Targets& targets, const geom::Drawing& drawing,
                                      const MeshContext& mesh);

} // namespace fem::setup
