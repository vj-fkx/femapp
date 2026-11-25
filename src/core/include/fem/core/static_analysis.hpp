#pragma once

#include <fem/core/beam_result.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/model.hpp>
#include <fem/core/run_control.hpp>

#include <Eigen/Core>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fem::core {

// A problem that keeps a model from being solved: a full sentence that names
// the item involved (REQ-007-14).
struct ModelIssue
{
    std::string message;
};

// REQ-007-09, 10, 14: every problem found, in model order; empty when the
// model can be solved. Elements of dimension 0 (points) need no assignment:
// they only define groups of nodes.
[[nodiscard]] std::vector<ModelIssue> validate(const Model& model);

// REQ-007-12, 008-21: displacements and reactions per node, forces per bar.
struct StaticResult
{
    std::shared_ptr<const Mesh> mesh;
    std::vector<Eigen::Vector3d> displacements;   // per node: u, v, w (0 without dof)
    std::vector<Eigen::Vector3d> reactions;       // per node: 0 where free
    std::vector<Eigen::Vector3d> rotations;       // per node: rx, ry, rz (0 without dof)
    std::vector<Eigen::Vector3d> reactionMoments; // per node: 0 where free
    // REQ-008d-07: per node, the warping (rate of twist) and the reaction
    // bimoment where it is restrained; 0 without the dof.
    std::vector<double> warping;
    std::vector<double> reactionBimoments;
    // Per element; NaN for elements that are not bars. For beams, at the
    // start of the flexible length.
    std::vector<double> axialForce; // N, tension positive
    std::vector<double> strain;     // mechanical: N / EA
    std::vector<double> stress;     // N / A
    // Per element: the forces along each beam; empty for the others.
    std::vector<std::optional<BeamResult>> beams;
    // REQ-011-13: the resultant of the loads (the assembled load vector
    // summed over the u, v, w dofs, settlements apart), which the reactions
    // balance; zero for a buckling mode.
    Eigen::Vector3d appliedForce = Eigen::Vector3d::Zero();
};

struct StaticOptions
{
    int threads = 0;      // 0: all cores; 1: sequential
    RunControl control{}; // REQ-011-09; second order and buckling carry it in `base`
};

// REQ-007-12, 13, 15: linear static analysis of the model, which is only
// read. Fails with the issues of validate() (one per line) or with the node
// and direction of a mechanism.
[[nodiscard]] Expected<StaticResult> runLinearStatic(const Model& model,
                                                     const StaticOptions& options = {});

} // namespace fem::core
