#pragma once

#include <fem/core/beam_loads.hpp>
#include <fem/core/section.hpp>

#include <Eigen/Core>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

struct IsoBeamData;

// REQ-008-21, 22: one beam after the analysis.
class BeamResult
{
public:
    // What the evaluation needs; filled by the analysis.
    struct Data
    {
        double length = 0.0;                                // flexible
        Eigen::Matrix3d axes = Eigen::Matrix3d::Identity(); // principal, columns, global
        BeamData beam;                                      // stiffnesses, shear centre, foundation
        SpanLoads loads;                                    // span loads and imposed strains
        BeamVector displacements = BeamVector::Zero();      // ends, local, centroid
        BeamForces start;                                   // internal forces at x = 0
        BeamForces end;                                     // internal forces at x = length
        // REQ-008b-19: an exact beam (design 008b §5.3): the state engine
        // from the local dofs of both ends of its flexible length.
        bool exact = false;
        bool planar = false;
        StateBarVector ends;
        // REQ-008b-19: an isoparametric beam (design 008b §5.5): its data,
        // its nodes and the values of its nodal dofs.
        std::shared_ptr<const IsoBeamData> iso;
        ElementNodes isoNodes;
        std::vector<double> isoValues;
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

// REQ-008-23: the stresses at a point of the section.
struct PointStress
{
    std::string name;
    Eigen::Vector2d position = Eigen::Vector2d::Zero(); // principal (y', z'), from the centroid
    double normal = 0.0;                                // N/A - Mz y'/Iz + My z'/Iy
    double shear = 0.0;    // |tau_V| + |tau_T|: the sum is on the safe side
    double vonMises = 0.0; // sqrt(sigma^2 + 3 tau^2)
};

// At the stress points of the shape of `section`; empty for a generic
// section or an impossible shape.
[[nodiscard]] std::vector<PointStress> sectionStresses(const Section& section,
                                                       const BeamForces& forces);
// The same for a solved section (REQ-008b-06): for a solid polygon, the
// shear at its vertices comes from the warping and shear functions, and is
// the size of the shear stress vector.
[[nodiscard]] std::vector<PointStress> sectionStresses(const SectionSolution& solution,
                                                       const BeamForces& forces);

// REQ-008b-06, solid polygon only: the stresses at a point (y, z) of the
// drawing, and the point of largest von Mises over the mesh (its corner and
// mid-side nodes, in each triangle). Without a solid polygon, only the
// normal stress.
[[nodiscard]] PointStress stressAt(const SectionSolution& solution, const BeamForces& forces,
                                   const Eigen::Vector2d& point);
[[nodiscard]] PointStress largestVonMises(const SectionSolution& solution,
                                          const BeamForces& forces);

// The normal stress at a point (y, z) of the local axes of the bar, from the
// centroid: for any section, the generic one included.
[[nodiscard]] double normalStress(const SectionProperties& section, const BeamForces& forces,
                                  const Eigen::Vector2d& point);

// Internal forces at both ends from the end forces of beamEnds() (forces
// that the nodes apply on the bar, on the centroid dofs).
void endForces(const BeamData& beam, const BeamVector& forces, BeamForces& start, BeamForces& end);

// REQ-008b-19: the same for an exact beam, from the forces that the nodes
// apply on its flexible length (design 008b §5.3).
void exactEndForces(const BeamData& beam, bool planar, const StateBarVector& forces,
                    BeamForces& start, BeamForces& end);

} // namespace fem::core
