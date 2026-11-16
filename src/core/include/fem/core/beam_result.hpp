#pragma once

#include <fem/core/beam_loads.hpp>

#include <Eigen/Core>
#include <cstdint>

namespace fem::core {

// REQ-008-21: internal forces of a beam, on the face of normal +x, in the
// principal local axes (design 008 §5.7). N > 0 is tension; Mz > 0 stretches
// the fibres of y < 0: sigma = N/A - Mz y/Iz + My z/Iy. T is about the shear
// centre.
struct BeamForces
{
    double n = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double t = 0.0;
    double my = 0.0;
    double mz = 0.0;
};

// Forces, and displacements and rotations of the centroid axis, at a point.
struct BeamPoint
{
    BeamForces forces;
    Eigen::Vector3d displacement = Eigen::Vector3d::Zero(); // principal local axes
    Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
};

// At a concentrated load: the value just before or just after it.
enum class Side : std::uint8_t { Before, After };

// REQ-008-21, 22: one beam after the analysis.
class BeamResult
{
public:
    // What the evaluation needs; filled by the analysis.
    struct Data
    {
        double length = 0.0; // flexible
        Eigen::Matrix3d axes = Eigen::Matrix3d::Identity(); // principal, columns, global
        BeamData beam;       // stiffnesses, shear centre, foundation
        SpanLoads loads;     // span loads and imposed strains
        BeamVector displacements = BeamVector::Zero(); // ends, local, centroid
        BeamForces start;    // internal forces at x = 0
        BeamForces end;      // internal forces at x = length
    };

    explicit BeamResult(Data data);

    [[nodiscard]] double length() const { return m_data.length; }
    [[nodiscard]] const Eigen::Matrix3d& axes() const { return m_data.axes; }
    [[nodiscard]] const BeamForces& start() const { return m_data.start; }
    [[nodiscard]] const BeamForces& end() const { return m_data.end; }

    // x in [0, length()] from the start of the flexible length. Exact for the
    // span loads within the theory; approximate on an elastic foundation
    // (REQ-008-15).
    [[nodiscard]] BeamPoint at(double x, Side side = Side::After) const;

private:
    Data m_data;
};

// Internal forces at both ends from the end forces of beamEnds() (forces
// that the nodes apply on the bar, on the centroid dofs).
void endForces(const BeamData& beam, const BeamVector& forces, BeamForces& start, BeamForces& end);

} // namespace fem::core
