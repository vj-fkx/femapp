#pragma once

#include <fem/core/expected.hpp>

#include <Eigen/Core>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fem::core {

// REQ-008-03: parametric shapes. Each is drawn in the (y, z) plane of the
// bar section, with the height h along y and the width b along z
// (design 008 §5.1).
struct Rectangle
{
    double b = 0.0;
    double h = 0.0;
};

struct Circle
{
    double d = 0.0;
};

struct CircularTube
{
    double d = 0.0; // outer diameter
    double t = 0.0; // wall
};

struct RectangularTube
{
    double b = 0.0;
    double h = 0.0;
    double t = 0.0; // wall, the same on all sides
};

// Doubly symmetric I: flanges at +y and -y, web along y.
struct ISection
{
    double h = 0.0;
    double bf = 0.0;
    double tf = 0.0;
    double tw = 0.0;
};

// Unequal angle: one leg of length h along y, the other of length b along z,
// both of thickness t, the outer corner at the origin.
struct Angle
{
    double b = 0.0;
    double h = 0.0;
    double t = 0.0;
};

// Channel: web along y at z = 0, flanges toward +z.
struct Channel
{
    double h = 0.0;
    double bf = 0.0;
    double tf = 0.0;
    double tw = 0.0;
};

// Tee: flange at the top (+y), stem along y.
struct Tee
{
    double h = 0.0;
    double bf = 0.0;
    double tf = 0.0;
    double tw = 0.0;
};

using SectionShape = std::variant<Rectangle, Circle, CircularTube, RectangularTube, ISection,
                                  Angle, Channel, Tee>;

// REQ-007-02, 008-02: a bar or beam section. Either a generic section, by its
// properties about axes through the centroid parallel to the local y and z
// of the bar, or a shape from which they are computed; giving both is a
// problem of the model. A truss only needs the area.
struct Section
{
    std::string name;
    std::optional<double> area{};             // A
    std::optional<double> inertiaY{};         // Iy = integral of z^2
    std::optional<double> inertiaZ{};         // Iz = integral of y^2
    std::optional<double> productOfInertia{}; // Iyz = integral of y z (default 0)
    std::optional<double> torsionConstant{};  // J
    std::optional<Eigen::Vector2d> shearCentre{}; // (y, z) from the centroid (default 0)
    std::optional<double> shearAreaY{};           // Asy, for the shear force along y
    std::optional<double> shearAreaZ{};           // Asz
    std::optional<SectionShape> shape{};
};

// Everything a beam needs from its section, about the principal axes
// (design 008 §5.1). Missing generic properties are zero; missing shear
// areas stay empty.
struct SectionProperties
{
    double area = 0.0;
    Eigen::Vector2d centroid = Eigen::Vector2d::Zero(); // in the drawing (y, z)
    double principalAngle = 0.0; // from y to the principal y', in (-pi/4, pi/4]
    double inertiaY = 0.0;       // integral of z'^2: bending by My, in the x-z' plane
    double inertiaZ = 0.0;       // integral of y'^2: bending by Mz, in the x-y' plane
    double torsionConstant = 0.0;
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // principal (y', z'), from the centroid
    std::optional<double> shearAreaY{};
    std::optional<double> shearAreaZ{};
};

// REQ-008-03, 04, 05: computed for a shape, rotated to the principal axes
// for a generic section. Fails, naming the section and the dimension, for an
// impossible shape.
[[nodiscard]] Expected<SectionProperties> sectionProperties(const Section& section);

// REQ-008-23: a characteristic point of a shape for the stresses (design 008
// §5.1). The shear of the shear forces follows Jourawski, tau = V Q / (I t),
// where firstMoment and thickness are given; the shear of the torque is
// tau = T * torsionFactor.
struct StressPoint
{
    std::string name;
    Eigen::Vector2d position = Eigen::Vector2d::Zero(); // principal (y', z'), from the centroid
    double firstMomentY = 0.0; // Q for Vy (about z), 0 where not computed
    double thicknessY = 0.0;
    double firstMomentZ = 0.0; // Q for Vz (about y)
    double thicknessZ = 0.0;
    double torsionFactor = 0.0;
};

// The points of a valid shape: the extreme fibres, the points of largest
// shear and of largest torsional shear. The angle only has its corners and
// the torsional shear (no simple shear flow in its principal axes).
[[nodiscard]] std::vector<StressPoint> stressPoints(const SectionShape& shape);

} // namespace fem::core
