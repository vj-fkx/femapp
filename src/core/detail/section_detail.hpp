#pragma once

// Pieces of the section computations shared between section.cpp and the
// sources of the thin-walled and solid sections (design 008b §5.1).

#include <fem/core/expected.hpp>
#include <fem/core/section.hpp>

#include <Eigen/Core>
#include <string_view>
#include <vector>

namespace fem::core::detail {

// Area, centroid and second moments about the centroid, in the drawing.
struct SectionGeometry
{
    double area = 0.0;
    Eigen::Vector2d centroid = Eigen::Vector2d::Zero();
    double iy = 0.0;  // integral of z^2
    double iz = 0.0;  // integral of y^2
    double iyz = 0.0; // integral of y z
};

// The principal angle closest to the drawing axes: tan 2 theta = 2 Iyz / (Iz - Iy),
// in (-pi/4, pi/4].
[[nodiscard]] double principalAngle(double iy, double iz, double iyz);

// A vector of the drawing in the principal axes turned by theta.
[[nodiscard]] Eigen::Vector2d toPrincipal(const Eigen::Vector2d& v, double theta);

// REQ-008b-03: a thin-walled section on its mid-line (design 008b §5.1.2).
struct ThinWalledData
{
    SectionGeometry geometry;
    double torsion = 0.0;                                  // J
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // drawing, absolute
    double warping = 0.0;                                  // Iw about the shear centre
    double shearAreaY = 0.0;                               // principal y'
    double shearAreaZ = 0.0;                               // principal z'
    // Per point: omega about the shear centre, normalised (0 for a point
    // that no wall uses).
    std::vector<double> sectorial;
};

// Fails, naming the section and the point or wall, for an invalid mid-line
// (REQ-008b-04).
[[nodiscard]] Expected<ThinWalledData> thinWalled(const ThinWalled& shape,
                                                  std::string_view section);

} // namespace fem::core::detail
