#pragma once

// Pieces of the section computations shared between section.cpp and the
// sources of the thin-walled and solid sections (design 008b §5.1).

#include <fem/core/detail/section_mesh.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/section.hpp>

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <memory>
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
    // Per wall, at its `from` and `to` ends, along `from` to `to`: the shear
    // flow of a unit shear force along y' and along z' (REQ-008b-06).
    std::vector<std::array<double, 2>> shearFlowY;
    std::vector<std::array<double, 2>> shearFlowZ;
    // Per wall: the torsion flow of the cells for a unit twist rate and G = 1
    // (0 on the open branches), and whether the wall is in a cell.
    std::vector<double> torsionFlow;
    std::vector<bool> inCell;
};

// Fails, naming the section and the point or wall, for an invalid mid-line
// (REQ-008b-04).
[[nodiscard]] Expected<ThinWalledData> thinWalled(const ThinWalled& shape,
                                                  std::string_view section);

// I'z (aboutZ) or I'y of the principal axes turned by theta.
[[nodiscard]] double toPrincipalInertia(const SectionGeometry& g, double theta, bool aboutZ);

// REQ-008b-02, 06: the fields of a solid polygon section on its mesh, kept
// for the stresses (design 008b §5.1.3). Nodal values.
struct SolidField
{
    SectionMesh mesh;
    Eigen::Vector2d centroid = Eigen::Vector2d::Zero(); // drawing
    double principalAngle = 0.0;
    Eigen::VectorXd phi;   // Saint-Venant warping, pole at the centroid
    Eigen::VectorXd omega; // sectorial coordinate about the shear centre, normalised
    Eigen::VectorXd chiY;  // shear function of a shear force along y'
    Eigen::VectorXd chiZ;  // along z'
};

struct SolidData
{
    SectionGeometry geometry;
    double torsion = 0.0;
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // drawing, absolute
    double warping = 0.0;
    double shearAreaY = 0.0; // principal y'
    double shearAreaZ = 0.0;
    std::shared_ptr<const SolidField> field;
};

// Fails as meshSection does (REQ-008b-04).
[[nodiscard]] Expected<SolidData> solidSection(const SolidPolygon& shape, std::string_view section);

// One six-node triangle of the section mesh (straight sides).
struct SolidElement
{
    std::array<int, 6> nodes{};
    double area = 0.0;
    std::array<Eigen::Vector2d, 3> corners{Eigen::Vector2d::Zero(), Eigen::Vector2d::Zero(),
                                           Eigen::Vector2d::Zero()};
    std::array<Eigen::Vector2d, 3> gradL{Eigen::Vector2d::Zero(), Eigen::Vector2d::Zero(),
                                         Eigen::Vector2d::Zero()};
};

// The quadratic shape functions at area coordinates l, their gradients in
// the drawing and the point.
struct SolidShape
{
    Eigen::Matrix<double, 6, 1> n = Eigen::Matrix<double, 6, 1>::Zero();
    Eigen::Matrix<double, 2, 6> gradient = Eigen::Matrix<double, 2, 6>::Zero();
    Eigen::Vector2d position = Eigen::Vector2d::Zero();
};

[[nodiscard]] SolidElement solidElement(const SectionMesh& mesh, std::size_t triangle);
[[nodiscard]] SolidShape solidShape(const SolidElement& element, const std::array<double, 3>& l);

} // namespace fem::core::detail
