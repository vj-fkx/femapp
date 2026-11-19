#pragma once

#include <fem/core/expected.hpp>

#include <Eigen/Core>
#include <memory>
#include <optional>
#include <span>
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

// REQ-008b-03: a thin-walled section by its mid-line: straight walls between
// points of the drawing, each with its thickness, forming open branches and
// closed cells (design 008b §5.1.2). Points and walls are numbered from 0.
struct ThinWalled
{
    struct Wall
    {
        int from = 0;
        int to = 0;
        double t = 0.0;
    };
    std::vector<Eigen::Vector2d> points;
    std::vector<Wall> walls;
};

// REQ-008b-02: a solid section of one material: regions, each an outline
// with holes, as polygons of the drawing (any orientation). J, the shear
// centre, Iw and the shear areas come from a triangulation of its own
// (design 008b §5.1.3); meshSize is its largest edge (0: the default).
struct SolidPolygon
{
    struct Region
    {
        std::vector<Eigen::Vector2d> outline;
        std::vector<std::vector<Eigen::Vector2d>> holes;
    };
    std::vector<Region> regions;
    double meshSize = 0.0;
};

using SectionShape = std::variant<Rectangle, Circle, CircularTube, RectangularTube, ISection, Angle,
                                  Channel, Tee, ThinWalled, SolidPolygon>;

// REQ-007-02, 008-02: a bar or beam section. Either a generic section, by its
// properties about axes through the centroid parallel to the local y and z
// of the bar, or a shape from which they are computed; giving both is a
// problem of the model. A truss only needs the area.
struct Section
{
    std::string name;
    std::optional<double> area = std::nullopt;             // A
    std::optional<double> inertiaY = std::nullopt;         // Iy = integral of z^2
    std::optional<double> inertiaZ = std::nullopt;         // Iz = integral of y^2
    std::optional<double> productOfInertia = std::nullopt; // Iyz = integral of y z (default 0)
    std::optional<double> torsionConstant = std::nullopt;  // J
    std::optional<Eigen::Vector2d> shearCentre =
        std::nullopt;                                // (y, z) from the centroid (default 0)
    std::optional<double> shearAreaY = std::nullopt; // Asy, for the shear force along y
    std::optional<double> shearAreaZ = std::nullopt; // Asz
    std::optional<SectionShape> shape = std::nullopt;
    // REQ-008b-01: Iw about the shear centre, for beams with warping.
    std::optional<double> warpingConstant = std::nullopt;
    // REQ-008d-12: the monosymmetry coefficients beta_y and beta_z of
    // SectionProperties, in the principal axes. A generic section without
    // them is taken as doubly symmetric (both zero).
    std::optional<double> monosymmetryY = std::nullopt;
    std::optional<double> monosymmetryZ = std::nullopt;
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
    std::optional<double> shearAreaY = std::nullopt;
    std::optional<double> shearAreaZ = std::nullopt;
    double warpingConstant = 0.0; // REQ-008b-01: Iw, about the shear centre
    // REQ-008d-12 (design 008d §5.7), principal axes from the centroid with
    // (ys, zs) the shear centre: r0^2 = (Iy + Iz) / A + ys^2 + zs^2, the
    // polar radius about the shear centre, squared, and the monosymmetry
    // coefficients (Trahair's beta, zero for doubly symmetric sections)
    // beta_y = integral of z (y^2 + z^2) dA / Iy - 2 zs and
    // beta_z = integral of y (y^2 + z^2) dA / Iz - 2 ys.
    double polarRadius2 = 0.0;
    double monosymmetryY = 0.0;
    double monosymmetryZ = 0.0;
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
    double firstMomentY = 0.0;                          // Q for Vy (about z), 0 where not computed
    double thicknessY = 0.0;
    double firstMomentZ = 0.0; // Q for Vz (about y)
    double thicknessZ = 0.0;
    double torsionFactor = 0.0;
    // REQ-008b-01, 20: the sectorial coordinate omega about the shear centre,
    // normalised (integral of omega dA = 0), with d omega = y dz - z dy along
    // the mid-line; the warping displacement is -omega theta'.
    double sectorial = 0.0;
    // REQ-008d-09: the sectorial first moment S_omega (the integral of
    // omega t ds from a free end) and the wall thickness there, for the
    // shear of the warping torque, tau_w = Tw S_omega / (Iw t).
    double sectorialMoment = 0.0;
    double sectorialThickness = 0.0;
};

namespace detail {
struct SolidField;
} // namespace detail

// REQ-008b-02, 06: a section solved once (design 008b §5.1): its properties,
// its stress points and, for a solid polygon, the fields on its mesh.
class SectionSolution
{
public:
    SectionSolution(SectionProperties properties, std::vector<StressPoint> points,
                    std::shared_ptr<const detail::SolidField> field = nullptr);

    [[nodiscard]] const SectionProperties& properties() const { return m_properties; }
    [[nodiscard]] std::span<const StressPoint> points() const { return m_points; }
    // Null except for a solid polygon.
    [[nodiscard]] const detail::SolidField* field() const { return m_field.get(); }

private:
    SectionProperties m_properties;
    std::vector<StressPoint> m_points;
    std::shared_ptr<const detail::SolidField> m_field;
};

// Solves the section; fails as sectionProperties does.
[[nodiscard]] Expected<std::shared_ptr<const SectionSolution>> solveSection(const Section& section);

// The points of a valid shape: the extreme fibres, the points of largest
// shear and of largest torsional shear. The angle only has its corners and
// the torsional shear (no simple shear flow in its principal axes).
[[nodiscard]] std::vector<StressPoint> stressPoints(const SectionShape& shape);

} // namespace fem::core
