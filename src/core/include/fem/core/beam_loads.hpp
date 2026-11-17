#pragma once

#include <fem/core/beam_element.hpp>
#include <fem/core/model.hpp>

#include <Eigen/Core>
#include <vector>

namespace fem::core {

// REQ-008-16 to 19: the loads of the span of one beam, in the principal local
// axes, positions along the flexible length (design 008 §5.5). Forces act on
// the centroid; the torque of their lever about the shear centre is already
// in mx.

// A load per length, linear from `from` to `to`: (qx, qy, qz, mx).
struct SpanPiece
{
    double from = 0.0;
    double to = 0.0;
    Eigen::Vector4d start = Eigen::Vector4d::Zero();
    Eigen::Vector4d end = Eigen::Vector4d::Zero();
};

// A force and a moment at one point.
struct SpanPoint
{
    double at = 0.0;
    Eigen::Vector3d force = Eigen::Vector3d::Zero();
    Eigen::Vector3d moment = Eigen::Vector3d::Zero();
};

// REQ-008b-15: a force per length fixed in space (a global load, the weight)
// on a curved bar, linear from `from` to `to`, by its components in the
// principal local axes at the start of the bar; along the arc its local
// components turn with the frame (design 008b §5.3). On the centroid.
struct SpanFixedPiece
{
    double from = 0.0;
    double to = 0.0;
    Eigen::Vector3d start = Eigen::Vector3d::Zero();
    Eigen::Vector3d end = Eigen::Vector3d::Zero();
};

struct SpanLoads
{
    std::vector<SpanPiece> pieces;
    std::vector<SpanPoint> points;
    std::vector<SpanFixedPiece> fixedPieces; // curved bars only
    double imposedStrain = 0.0;              // alpha dT + eps0
    double curvatureY = 0.0;                 // imposed: My = EIy (ry' - curvatureY)
    double curvatureZ = 0.0;                 // imposed: Mz = EIz (rz' - curvatureZ)
};

// Where a load on a bar is: its local axes, as the user sees them (after the
// roll), and the turn to the principal axes of the section.
struct SpanFrame
{
    Eigen::Matrix3d principalAxes = Eigen::Matrix3d::Identity(); // columns, global
    double principalAngle = 0.0;                           // from the local y to the principal y'
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // principal, from the centroid
};

// A position along the flexible length.
[[nodiscard]] double positionOn(const BarPosition& position, double length);

// startTorque and endTorque: a torque per length about the local x
// (REQ-008b-17), with the forces.
void addDistributed(SpanLoads& loads, const SpanFrame& frame, Axes axes,
                    const Eigen::Vector3d& startValue, const Eigen::Vector3d& endValue, double from,
                    double to, double startTorque = 0.0, double endTorque = 0.0);
void addConcentrated(SpanLoads& loads, const SpanFrame& frame, Axes axes,
                     const Eigen::Vector3d& force, const Eigen::Vector3d& moment, double at);
// The gradients dT/dy, dT/dz of the local axes, as imposed curvatures.
void addGradient(SpanLoads& loads, const SpanFrame& frame, double alpha, double alongY,
                 double alongZ);

// The loads of the span held at both ends, as local dofs at the centroid:
// the integral of N^T q with the functions of the theory, plus the imposed
// strains (design 008 §5.5). This is BeamData::fixedEndLoads.
[[nodiscard]] BeamVector equivalentLoads(const BeamData& beam, double length,
                                         const SpanLoads& loads);

} // namespace fem::core
