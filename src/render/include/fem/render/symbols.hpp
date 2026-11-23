#pragma once

// Feature 010 (design §4, §5.6; REQ-010-09, 22): the symbols of the model
// drawn over the viewport: supports, loads and local axes, in global
// coordinates, each of a constant size on the screen.

#include <Eigen/Core>
#include <cstdint>
#include <vector>

namespace fem::render {

// A support at a node. held: bit k for core::DofType k (U, V, W, RX, RY, RZ,
// T, Warp); planar: a 2D model, where only U, V and RZ count.
struct SupportSymbol
{
    Eigen::Vector3d at = Eigen::Vector3d::Zero();
    std::uint8_t held = 0;
    bool planar = false;
};

// A force at a node or in a span: the arrow ends at `at`.
struct ArrowSymbol
{
    Eigen::Vector3d at = Eigen::Vector3d::Zero();
    Eigen::Vector3d vector = Eigen::Vector3d::Zero();
};

// A moment: an arc about `axis` (right-hand rule).
struct MomentSymbol
{
    Eigen::Vector3d at = Eigen::Vector3d::Zero();
    Eigen::Vector3d axis = Eigen::Vector3d::Zero();
};

// A load distributed from `from` to `to`, varying linearly, per length.
struct DistributedSymbol
{
    Eigen::Vector3d from = Eigen::Vector3d::Zero();
    Eigen::Vector3d to = Eigen::Vector3d::Zero();
    Eigen::Vector3d startValue = Eigen::Vector3d::Zero();
    Eigen::Vector3d endValue = Eigen::Vector3d::Zero();
};

// The local axes of a bar: columns x, y, z.
struct AxesSymbol
{
    Eigen::Vector3d at = Eigen::Vector3d::Zero();
    Eigen::Matrix3d axes = Eigen::Matrix3d::Identity();
};

struct SymbolSet
{
    std::vector<SupportSymbol> supports;
    std::vector<ArrowSymbol> forces;
    std::vector<MomentSymbol> moments;
    std::vector<DistributedSymbol> distributed;
    std::vector<AxesSymbol> axes;

    [[nodiscard]] bool empty() const noexcept
    {
        return supports.empty() && forces.empty() && moments.empty() && distributed.empty() &&
               axes.empty();
    }
};

// REQ-010-22: the glyph of a support by the pattern of the dofs held.
// Fixed: every translation and rotation; Pinned: the translations only;
// Roller: one translation only, `up` along it; anything else is Generic.
// Fixed and Pinned stand on the global Y.
enum class SupportGlyph : std::uint8_t { Fixed, Pinned, Roller, Generic };

struct SupportShape
{
    SupportGlyph glyph = SupportGlyph::Generic;
    Eigen::Vector3d up = Eigen::Vector3d::UnitY();
};

[[nodiscard]] SupportShape supportShape(std::uint8_t held, bool planar);

} // namespace fem::render
